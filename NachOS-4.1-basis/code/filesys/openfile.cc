// openfile.cc
//	Routines to manage an open Nachos file.  As in UNIX, a
//	file must be open before we can read or write to it.
//	Once we're all done, we can close it (in Nachos, by deleting
//	the OpenFile data structure).
//
//	Also as in UNIX, for convenience, we keep the file header in
//	memory while the file is open.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "main.h"
#include "filehdr.h"
#include "openfile.h"
#include "synchdisk.h"

//----------------------------------------------------------------------
// OpenFile::OpenFile
// 	Open a Nachos file for reading and writing.  Bring the file header
//	into memory while the file is open.
//
//	"sector" -- the location on disk of the file header for this file
//----------------------------------------------------------------------

#ifndef FILESYS_STUB
OpenFile::OpenFile(int sector)
{
    hdr = new FileHeader;
    hdr->FetchFrom(sector);
    seekPosition = 0;
}
#else
OpenFile::OpenFile(int f)
{
    file = f; seekPosition = 0;
}
#endif

//----------------------------------------------------------------------
// OpenFile::~OpenFile
// 	Close a Nachos file, de-allocating any in-memory data structures.
//----------------------------------------------------------------------

OpenFile::~OpenFile()
{
#ifndef FILESYS_STUB
    delete hdr;
#else
    Close(file);
#endif
}

//----------------------------------------------------------------------
// OpenFile::Seek
// 	Change the current location within the open file -- the point at
//	which the next Read or Write will start from.
//
//	"position" -- the location within the file for the next Read/Write
//----------------------------------------------------------------------
#ifndef FILESYS_STUB
void
OpenFile::Seek(int position)
{
    seekPosition = position;
}
#endif

//----------------------------------------------------------------------
// OpenFile::Read/Write
// 	Read/write a portion of a file, starting from seekPosition.
//	Return the number of bytes actually written or read, and as a
//	side effect, increment the current position within the file.
//
//	Implemented using the more primitive ReadAt/WriteAt.
//
//	"into" -- the buffer to contain the data to be read from disk
//	"from" -- the buffer containing the data to be written to disk
//	"numBytes" -- the number of bytes to transfer
//----------------------------------------------------------------------

int
OpenFile::Read(char *into, int numBytes)
{
   int result = ReadAt(into, numBytes, seekPosition);
   seekPosition += result;
   return result;
}

int
OpenFile::Write(char *into, int numBytes)
{
   int result = WriteAt(into, numBytes, seekPosition);
   seekPosition += result;
   return result;
}

//----------------------------------------------------------------------
// OpenFile::ReadAt/WriteAt
// 	Read/write a portion of a file, starting at "position".
//	Return the number of bytes actually written or read, but has
//	no side effects (except that Write modifies the file, of course).
//
//	There is no guarantee the request starts or ends on an even disk sector
//	boundary; however the disk only knows how to read/write a whole disk
//	sector at a time.  Thus:
//
//	For ReadAt:
//	   We read in all of the full or partial sectors that are part of the
//	   request, but we only copy the part we are interested in.
//	For WriteAt:
//	   We must first read in any sectors that will be partially written,
//	   so that we don't overwrite the unmodified portion.  We then copy
//	   in the data that will be modified, and write back all the full
//	   or partial sectors that are part of the request.
//
//	"into" -- the buffer to contain the data to be read from disk
//	"from" -- the buffer containing the data to be written to disk
//	"numBytes" -- the number of bytes to transfer
//	"position" -- the offset within the file of the first byte to be
//			read/written
//----------------------------------------------------------------------

int
OpenFile::ReadAt(char *into, int numBytes, int position)
{
#ifndef FILESYS_STUB
    int fileLength = hdr->FileLength();
#else
    int fileLength = Length();
#endif
    int i, firstSector, lastSector, numSectors;
    char *buf;

    if ((numBytes <= 0) || (position >= fileLength))
    	return 0; 				// check request
    if ((position + numBytes) > fileLength)
	numBytes = fileLength - position;
    DEBUG(dbgFile, "Reading " << numBytes << " bytes at " << position << " from file of length " << fileLength);

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

    // read in all the full and partial sectors that we need
#ifndef FILESYS_STUB
    buf = new char[numSectors * SectorSize];
    for (i = firstSector; i <= lastSector; i++)
        kernel->synchDisk->ReadSector(hdr->ByteToSector(i * SectorSize),
					&buf[(i - firstSector) * SectorSize]);

    // copy the part we want
    bcopy(&buf[position - (firstSector * SectorSize)], into, numBytes);
    delete [] buf;
    return numBytes;
#else
    for (i = firstSector; i <= lastSector; i++)
        kernel->synchDisk->ReadSector(i,NULL);
    Lseek(file, position, 0);
    return ReadPartial(file, into, numBytes);
#endif
}

int
OpenFile::WriteAt(char *from, int numBytes, int position)
{
  #ifndef FILESYS_STUB
      int fileLength = hdr->FileLength();
  #else
      int fileLength = Length();
  #endif
    int i, firstSector, lastSector, numSectors;
    bool firstAligned, lastAligned;
    char *buf;

  #ifndef FILESYS_STUB
    if ((numBytes <= 0) || (position >= fileLength))
	return 0;				// check request

    if ((position + numBytes) > fileLength)
	numBytes = fileLength - position;
  #else
    if ((numBytes <= 0))
        return 0;				// check request
  #endif

    DEBUG(dbgFile, "Writing " << numBytes << " bytes at " << position << " from file of length " << fileLength);

    firstSector = divRoundDown(position, SectorSize);
    lastSector = divRoundDown(position + numBytes - 1, SectorSize);
    numSectors = 1 + lastSector - firstSector;

#ifndef FILESYS_STUB
    buf = new char[numSectors * SectorSize];

    firstAligned = (position == (firstSector * SectorSize));
    lastAligned = ((position + numBytes) == ((lastSector + 1) * SectorSize));
// read in first and last sector, if they are to be partially modified
    if (!firstAligned)
        ReadAt(buf, SectorSize, firstSector * SectorSize);
    if (!lastAligned && ((firstSector != lastSector) || firstAligned))
        ReadAt(&buf[(lastSector - firstSector) * SectorSize],
				SectorSize, lastSector * SectorSize);

// copy in the bytes we want to change
    bcopy(from, &buf[position - (firstSector * SectorSize)], numBytes);

// write modified sectors back
    for (i = firstSector; i <= lastSector; i++)
        kernel->synchDisk->WriteSector(hdr->ByteToSector(i * SectorSize),
					&buf[(i - firstSector) * SectorSize]);
    delete [] buf;
    return numBytes;

#else
// write modified sectors back
    for (i = firstSector; i <= lastSector; i++)
        kernel->synchDisk->WriteSector(i, NULL);
    Lseek(file, position, 0);
    WriteFile(file, from, numBytes);
    return numBytes;
#endif
}

//----------------------------------------------------------------------
// OpenFile::Length
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------
#ifndef FILESYS_STUB
int
OpenFile::Length()
{
    return hdr->FileLength();
}
#else
int
OpenFile::Length()
{
  Lseek(file, 0, 2); return Tell(file);
}
#endif
