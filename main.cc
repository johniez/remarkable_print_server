#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include <time.h>
#include <getopt.h>
#include <uuid/uuid.h>

#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>

// Listen backlog size
#define BACKLOG 10


/// Help string to print usage on the terminal.
void printHelp(const std::string &binName) {
    std::cerr << "Usage: " << binName << " [OPTIONS]\n";
    std::cerr << "Listen on specified port and receive PDF printed data.\n";
    std::cerr << "Data name is UUID based, with generated metadata for rM2 device.\n\n";
    std::cerr << "  -p, --port NUM     Listen on given port. Default 9100.\n";
    std::cerr << "  -d, --dir DIR      Write PDF files into specified directory. "
                                      "Default /home/root/.local/share/remarkable/xochitl/\n";
    std::cerr << "  -h, --help         Show this help.\n";
}


/// Return UUID string with lowercase letters.
std::string uuid() {
    char buf[64];
    uuid_t uuidOut;
    uuid_generate(uuidOut);
    uuid_unparse_lower(uuidOut, buf);
    return buf;
}


/// PDF parsing state
enum class State {
    PARSING_HEADER,
    PARSING_BODY
};


/**
 * Get position in the data chunk pointing to the start of the PDF section.
 * Return std::string::npos if starting marker has not been found yet.
 *
 * \note It is expected to feed previous chunk line beginning in the chunkBorder.
 *       Even on first call, it has to be filled with dummy '\n'...
 * \param chunk  Current chunk data the pdf begin marker is searched in.
 * \param chunkBorder  Current line begin passed if chunk is smaller than line itself.
 */
std::string::size_type getPdfStart(const std::string &chunk, std::string &chunkBorder) {
    // how many chars we want to save from line begin when current chunk has only part of the line
    const unsigned MAX_PREV_BUFFERING = 1024;
    // create line always starting with '\n' (transfered from prev chunks)
    const std::string lineBuf{chunkBorder + chunk};
    // first new-line char at lineBuf - must be always 0 due to prepending chunkBorder
    std::string::size_type pos = lineBuf.find("\n");

    do {
        // create token the line is starting with
        const std::string lineToken = lineBuf.substr(pos, 6);
        // if line starts with that, pdf data starts just after next '\n'
        const bool pdfStartFound = (lineToken == std::string{"\n%PDF-"});
        if (pdfStartFound) {
            // search for next '\n' char where pdf data part begins
            const std::string::size_type nextNewLine = lineBuf.find("\n", pos + 1);
            if (nextNewLine == std::string::npos) {
                // save line begining to chunkBorder and pretend the pdf start was not found yet
                // the new line char is propably in next chunk, wait for it
                chunkBorder = lineBuf.substr(0, MAX_PREV_BUFFERING);
                return std::string::npos;
            }
            // return first char after new-line at the end of line with pdf start marker
            // chunkBorder must be subtracted, as it extended current chunk internally
            return nextNewLine - chunkBorder.size() + 1;
        }
        // get next new line char, if possible
        const std::string::size_type nextPos = lineBuf.find("\n", pos + 1);
        if (nextPos == std::string::npos) {
            // keep some content to the next chunk iteration in the future
            chunkBorder = lineBuf.substr(pos, MAX_PREV_BUFFERING);
            return std::string::npos;
        }
        pos = nextPos;
    } while (pos != std::string::npos);
    return std::string::npos;
}


/// Received file holder. It deletes written data on exception if close() was not called.
class ReceivedFile {
    /// Target directory
    std::string dataDir;
    /// PDF data output stream.
    std::ofstream file;
    /// File name (UUID without .pdf suffix)
    std::string fileUuid;
  public:
    explicit ReceivedFile(const std::string &dir): dataDir(dir), file(), fileUuid(uuid()) {
        if (dataDir.back() != '/') {
            dataDir.push_back('/');
        }
        file.open(std::string{dataDir + fileUuid + ".pdf"}.c_str(), std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error{"Cannot open file to write the pdf"};
        }
    }

    ~ReceivedFile() {
        if (file.is_open()) {
            file.close();
            unlink(std::string{dataDir + fileUuid + ".pdf"}.c_str());
        }
    }

    void close() {
        // write metadata first, it still can fail and throw an exception
        writeMetaData();
        // metadata successfuly written, close pdf file
        file.close();
    }

    std::string getFileNamePrefix() const {
        return fileUuid;
    }

    std::ofstream &operator<<(const std::string &data) {
        file << data;
        return file;
    }

  private:
    void writeMetaData() const {
        std::string metadata {
            "{\n"
            "    \"deleted\": false,\n"
            "    \"lastModified\": \"__MODIFIED_TIMESTAMP_MS__\",\n"
            "    \"metadatamodified\": true,\n"
            "    \"modified\": true,\n"
            "    \"parent\": \"\",\n"
            "    \"pinned\": false,\n"
            "    \"synced\": false,\n"
            "    \"type\": \"DocumentType\",\n"
            "    \"version\": 0,\n"
            "    \"visibleName\": \"PDF import\"\n"
            "}"
        };

        const std::string tsPattern{"__MODIFIED_TIMESTAMP_MS__"};
        metadata.replace(
            metadata.find(tsPattern), tsPattern.size(),
            std::to_string(::time(nullptr) * 1000));

        std::ofstream metadataFile{
            std::string{dataDir + fileUuid + ".metadata"}.c_str(), std::ios::binary};
        if (!metadataFile.is_open()) {
            throw std::runtime_error{"Failed to open metadata file"};
        }
        metadataFile << metadata;
        metadataFile.close();
    }
};


/**
 * Receive data from given socket and write PDF and metadata to output directory.
 * \param socket  Socket data are read from.
 * \param dir     Target directory to write PDF.
 */
void handlePdfFromSock(int socket, const std::string &dir) {
    ReceivedFile file{dir};
    std::cerr << "Receiving PDF into " << file.getFileNamePrefix() << ".pdf\n";

    State state = State::PARSING_HEADER;
    // buffer to pass line beginging through chunks - to simplify parsing start with '\n'
    std::string chunkBorder{"\n"};
    int received = 0;
    do {
        // receive data by some smaller chunks 
        char buf[1024];
        received = recv(socket, buf, sizeof(buf), 0);
        if (received > 0) {
            const std::string data{buf, static_cast<std::string::size_type>(received)};

            // search for pdf data begin
            if (state == State::PARSING_HEADER) {
                std::string::size_type pdfStart = getPdfStart(data, chunkBorder);
                if (pdfStart != std::string::npos) {
                    state = State::PARSING_BODY;
                    chunkBorder.clear();
                    // write first pdf data part into result file
                    file << data.substr(pdfStart);
                }
            } else {
                // write whole data chunk into result file
                file << data;
            }
        }
    } while (received > 0);

    // if some pdf data could be processed, close the file (will be discarded otherwise)
    if (state == State::PARSING_BODY) {
        file.close();
        std::cerr << "PDF imported.\n";
    } else {
        std::cerr << "PDF discarded.\n";
    }
}


/**
 * Get socket number of listening tcp server
 */
int getTcpServerSocket(const std::string &port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    // For wildcard IP address
    hints.ai_protocol = 0;          // Any protocol

    struct addrinfo *result;
    int status = getaddrinfo(NULL, port.c_str(), &hints, &result);
    if (status != 0) {
        std::cerr << "Error: getaddrinfo(): " << gai_strerror(status) << "\n";
        return -1;
    }

    int sock = -1;
    // iterate over getaddrinfo results and bind first capable socket
    for (const addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            // try another getaddrinfo result
            continue;
        }

        int reuseaddr = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1) {
            std::cerr << "Failed to set SO_REUSEADDR!\n";
            close(sock);
            return -1;
        }

        if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            // success, stop iteration
            break;
        }

        // cannot bind, close and try another one (if any)
        close(sock);
        sock = -1;
    }
    freeaddrinfo(result);

    if (sock == -1) {
        std::cerr << "Failed to create socket.\n";
        return -1;
    }

    if (listen(sock, BACKLOG) == -1) {
        std::cerr << "Failed to listne on socket.\n";
        return -1;
    }
    return sock;
}


/// Parse command line arguments, return false if help should be printed.
bool parseArgs(int argc, char *argv[], std::string &port, std::string &dir) {
    while (1) {
        static struct option long_options[] = {
            {"port", required_argument, 0, 'p'},
            {"dir",  required_argument, 0, 'd'},
            {"help", no_argument,       0, 'h'},
            {0,      0,                 0, 0}
        };

        const int c = getopt_long(argc, argv, "p:d:h", long_options, nullptr);
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'p':
                port = optarg;
                break;
            case 'd':
                dir = optarg;
                break;
            case 'h':
                return false;
            default:
                break;
        }
    }
    return true;
}


int main(int argc, char *argv[]) {
    std::string port{"9100"};
    std::string dir{"/home/root/.local/share/remarkable/xochitl/"};
    // parse options from command line
    if (!parseArgs(argc, argv, port, dir)) {
        printHelp(argv[0]);
        return 1;
    }

    // start basic tcp server
    const int sock = getTcpServerSocket(port);
    if (sock == -1) {
        return 1;
    }
 
    // process requests until process is killed
	while (1) {
		const int requestSock = accept4(sock, NULL, NULL, SOCK_CLOEXEC);
		if (requestSock == -1) {
            std::cerr << "Failed to accept connection!\n";
		} else {
            try {
                // write pdf received from socket into dir
    			handlePdfFromSock(requestSock, dir);
                // TODO restart xochitl
            } catch (const std::exception &e) {
                std::cerr << "PDF receive error: " << e.what() << "\n";
            }
            close(requestSock);
		}
	}

    close(sock);
    return 0;
}
