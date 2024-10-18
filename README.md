# tsbtap

**tsbtap** reads SIMH-format tape images that come from the 1970s-era
Hewlett-Packard (HP) Time-shared BASIC (TSB) 2000F and 2000 Access System
that are in that system's "dump" format.

**tsbtap** has options similar to the UNIX **tar** program for viewing tape
contents and extracting items from the tape.

**tstap** also has a limited ability to convert 2000F dump tapes to 2000
Access format and vice-versa.

## Extraction: specification

You can specify the file names to be extracted using shell-type wildcards,
e.g. "ida\*". **tsbtap** ignores upper/lower case when matching file names.

You can limit extraction to a particular user ID via the syntax
"*userid*/*filename*", e.g., "A000/HELLO".

## Extraction: file types

**tsbtap** can extract the following TSB file types:

- BASIC programs (including CSAVEd): extracted as ASCII text with a
filename ending in ".bas".

- BASIC-format files: extracted as comma-separated values, one line per
TSB record, with a filename ending in ".csv". The TSB logical EOF marker
is represented as " END" (without quotes) at the end of a line.

- ASCII files: extracted as text lines, with a filename ending in ".txt".

If the command-line specification does not include a user ID, **tsbtap**
creates a separate directory per user ID, e.g., the extracted files
from user ID "C903" are placed in a subdirectory named "C903".

**tsbtap** chooses a unique name for each extracted file. For example,
if "HELLO.bas" already exists, **tsbtap** will extract the next program
named "HELLO" to "HELLO.1.bas".

## Conversion caveats

The **tsbtap** conversion feature is experimental and may lead to unexpected
results, including data corruption and crashes, if the converted tape is used
on an HP 2000 TSB system.

General limitations:

- "Hibernate" and "sleep" tapes are silently converted to "dump" tapes.

- BASIC programs in CSAVE format are converted to non-CSAVE format.

## Converting 2000F to 2000 Access

In BASIC programs, ctrl-N and ctrl-O in quoted strings are replaced with
LF and CR. However:

- REM and IMAGE statements are copied unaltered.

- BASIC-formatted files are copied unaltered.

## Converting 2000 Access to 2000F

In BASIC programs, the "\*\*" operator is replaced with "^".

In BASIC programs, LF and CR in quoted strings are replaced with
ctrl-N and ctrl-O. However:

- REM and IMAGE statements are copied unaltered.

- BASIC-formatted files are copied unaltered.

Additional limitations:

- ASCII files are not copied to the converted tape.

- In BASIC programs, features added to 2000 Access (e.g., "LINPUT",
additional string variables "A0$", etc.) are not converted. Instead,
the problematic statement is converted to a REM.

## References

- SIMH tape format: http://www.bitsavers.org/pdf/simh/simh_magtape.pdf

- HP TSB documentation: http://bitsavers.org/pdf/hp/2000TSB/

- HP TSB dump tape images: http://bitsavers.org/bits/HP/tapes/2000tsb/
