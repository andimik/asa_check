# ASA ETI Check

`asa_check` (for Windows and Linux) reads raw 6144-byte DAB ETI(NI) frames and decodes the Emergency Warning System (EWS), also known as Automatic Safety Alert (ASA), defined by ETSI TS 104 089.

The decoder:

- validates FIB CRCs before inspecting FIG data;
- detects heartbeat and alert signalling in FIG 0/15;
- reports the alert phase, stage, incident ID, target ensemble, and audio SubChId;
- maps tuned-ensemble audio SubChIds to service IDs and DAB/DAB+ codecs using FIG 0/2;
- decodes DAB location codes and reports their geographic bounds; and
- suppresses repeated FIG transmissions while preserving alert state changes and later episodes.

ASA carries hierarchical alert-area codes, not GPS coordinates. The reported centre is derived from an area's bounds for convenience and is explicitly not a transmitted position.

# Windows 

## exe file for Windows (command line)

There is already a compiled file in `build-win32` subdirectory. If you anyway want to (cross-)compile it, see following chapter.

## Cross-compile under Linux for Windows 32bit

You need to install the 32-bit MinGW-w64 toolchain, CMake, and standard build utilities. Open your Linux terminal and run:

```bash
sudo apt update
sudo apt install mingw-w64 cmake make git
```

To tell CMake to build for 32-bit Windows instead of your host Linux system, you need to pass the MinGW compilers and target architecture flags. Run the following command to generate the build files:

```bash
cmake -S . -B build-win32 \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -static"
```

Once the configuration is complete, compile the project using CMake:

```bash
cmake --build build-win32
```

After the compilation completes successfully, you will find your 32-bit Windows executable inside the build folder:

```
ls -l build-win32/asa_check.exe
```

## Run under Windows in command prompt

```
asa_check.exe input.eti
```


# Linux

## Build instructions for Linux

You need to install the standard build utilities. Open your Linux terminal and run:

```
sudo apt update
sudo apt install cmake make git
```

Typical local build flow:

```bash
mkdir -p build
cd build
cmake ..
make
sudo make install
```

If you prefer to build in a single step from the project root:

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/asa_check input.eti
```

or

```bash
cat input.eti | ./build/asa_check
```

After installation, the binary is available as:

```bash
asa_check input.eti
```

Example ASA output (timestamp shown as a format placeholder):

```text
ASA detected at ETI frame 3542 (DAB time YYYY-MM-DD HH:MM:SS.mmm UTC)
Scope: tuned ensemble
FIG flags: C/N=0, OE=0, P/D=0
Phase: Trigger
Alert audio subchannel ID: 4
Audio service ID: 0xD004, codec: DAB+ (HE-AAC v2) (ASCTy 63)
Stage: Level 1 Start, incident ID: 9, last in group: yes
Location codes (0 FIG instance(s) follow):
	Z1:91BB82 bounds=[52.514648, 13.376953]..[52.523438, 13.385742] centre=52.519043, 13.381348
```

`Z1:91BB82` is an area in Berlin. The bounds and centre above are calculated from that code.

DAB time is the latest absolute UTC date and time carried by a valid FIG 0/10 from the tuned ensemble, not elapsed stream time or the computer's clock. Long form includes seconds and milliseconds; short form has minute precision. No time is extrapolated between clock updates. If no valid FIG 0/10 has been received before an ASA message, the output reports the DAB time as unavailable.

## ETSI Test Stream

ETSI publishes the receiver test streams at:

```text
https://docbox.etsi.org/Broadcast/Open/EWSv1_1.zip
```

The expected SHA-256 for the extracted `EWS1.eti` file is:

```text
b0b4787b212d4a1e79aabf2627c62d9e34f555ad296c949e43f476eb7d0c6ece
```

Run it with:

```bash
asa_check EWS1.eti
```

The current input reader supports raw ETI(NI) files or stdin. It does not unwrap network transport protocols or framed ETI containers.
