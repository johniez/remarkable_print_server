# PDF Print server for reMarkable tablet

Send PDF documents to your reMarkable tablet by printing over network.
Inspired by [Evidlo/remarkable_printer](https://github.com/Evidlo/remarkable_printer/),
this code is just reimplementation of Evidlo/remarkable_printer in C/C++ to keep tablet
resources as low as possible.

## Build

To build server from source, you need toolchain from remarkable.engeneering installed.

For example, you can download and install to your home directory:
```sh
wget https://remarkable.engineering/oecore-x86_64-cortexa9hf-neon-toolchain-zero-gravitas-1.8-23.9.2019.sh
chmod +x oecore-x86_64-cortexa9hf-neon-toolchain-zero-gravitas-1.8-23.9.2019.sh
./oecore-x86_64-cortexa9hf-neon-toolchain-zero-gravitas-1.8-23.9.2019.sh -d ~/rmtoolchain
```

And to use it (assuming you are in this repository cloned directory):
```sh
source ~/rmtoolchain/environment-setup-cortexa9hf-neon-oe-linux-gnueabi
make
```

The command above should set CXX variable to the toolchain compiler with arm architecture settings.
You can now use the binary in the tablet (after copying to it).

### Local build

For easier development you can omit toolchain setup and just use `make` command to build the server
for your own architecture (assuming linux). Beside c++ compiler it is necessary to have `uuid-dev`
library installed (deb package on debian/ubuntu). Keep in mind that resulting binary cannot be used
in the reMarkable device.

## Installation

You need the ssh access to your device. Transfer service and binary to your device, like:

```sh
ssh root@remarkable mkdir ./bin
scp pdf-print-server root@remarkable:./bin
scp pdf-print-server.service root@remarkable:/etc/systemd/system
```

Then run commands in your remarkable ssh shell:
```sh
systemctl daemon-reload
systemctl enable pdf-print-server.service
systemctl start pdf-print-server.service
```

That's it. Now you can see the server is listening on the specified port, by running `netstat -lt` on the remarkable shell.

### Printer setup

Just add a `Network Printer`/`AppSocket/HP JetDirect`, fill in IP address of your reMarkable tablet.
As a driver, choose `Generic`/`PDF`.

[Evidlo/remarkable_printer](https://github.com/Evidlo/remarkable_printer/#linux-manual) already have
documented printer settings by OS.

## Security

* Please keep in mind, that there is no authentication nor encryption involved. Use only in your own network.
* Server is currently running as `root` (as everything else in the rM tablet), so it has full access to your tablet!
* Use at own risk.


