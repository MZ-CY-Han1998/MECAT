/**************************************************************************
 * This file is part of Celera Assembler, a software program that
 * assembles whole-genome shotgun reads into contigs and scaffolds.
 * Copyright (C) 2005, J. Craig Venter Institute. All rights reserved.
 * Author: Brian Walenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received (LICENSE.txt) a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *************************************************************************/

//static char *rcsid = "$Id: AS_UTL_fileIO.C 4436 2013-09-27 20:26:53Z brianwalenz $";

#include "AS_UTL_fileIO.H"

//  Report ALL attempts to seek somewhere.
#undef DEBUG_SEEK

//  Use ftell() to verify that we wrote the expected number of bytes,
//  and that we ended up at the expected location.
#undef VERIFY_WRITE_POSITIONS

//  Provides a safe and reliable mechanism for reading / writing
//  binary data.
//
//  Split writes/reads into smaller pieces, check the result of each
//  piece.  Really needed by OSF1 (V5.1), useful on other platforms to
//  be a little more friendly (big writes are usually not
//  interruptable).

void
AS_UTL_safeWrite(FILE *file, const void *buffer, const char *desc, size_t size, size_t nobj) {
  size_t  position = 0;
  size_t  length   = 32 * 1024 * 1024 / size;
  size_t  towrite  = 0;
  size_t  written  = 0;

#ifdef VERIFY_WRITE_POSITIONS
  off_t   expectedposition = AS_UTL_ftell(file) + nobj * size;
  if (errno)
    //  If we return, and errno is set, the stream isn't seekable.
    expectedposition = 0;
#endif

  while (position < nobj) {
    towrite = length;
    if (position + towrite > nobj)
      towrite = nobj - position;

    errno = 0;
    written = fwrite(((char *)buffer) + position * size, size, towrite, file);

    if (errno) {
      fprintf(stderr, "safeWrite()-- Write failure on %s: %s\n", desc, strerror(errno));
      fprintf(stderr, "safeWrite()-- Wanted to write "F_SIZE_T" objects (size="F_SIZE_T"), wrote "F_SIZE_T".\n",
              towrite, size, written);
      assert(errno == 0);
    }

    position += written;
  }

  //  This catches a bizarre bug on FreeBSD (6.1 for sure, 4.10 too, I
  //  think) where we write at the wrong location; see fseek below.
  //
  //  UNFORTUNATELY, you can't ftell() on stdio.
  //
#ifdef VERIFY_WRITE_POSITIONS
  if ((expectedposition > 0) &&
      (AS_UTL_ftell(file) != expectedposition)) {
    fprintf(stderr, "safeWrite()-- EXPECTED "F_OFF_T", ended up at "F_OFF_T"\n",
            expectedposition, AS_UTL_ftell(file));
    assert(AS_UTL_ftell(file) == expectedposition);
  }
#endif
}


size_t
AS_UTL_safeRead(FILE *file, void *buffer, const char *desc, size_t size, size_t nobj) {
  size_t  position = 0;
  size_t  length   = 32 * 1024 * 1024 / size;
  size_t  toread   = 0;
  size_t  written  = 0;  //  readen?

  while (position < nobj) {
    toread = length;
    if (position + toread > nobj)
      toread = nobj - position;

    errno = 0;
    written = fread(((char *)buffer) + position * size, size, toread, file);
    position += written;

    if (feof(file) || (written == 0))
      goto finish;

    if ((errno) && (errno != EINTR)) {
      fprintf(stderr, "safeRead()-- Read failure on %s: %s.\n", desc, strerror(errno));
      fprintf(stderr, "safeRead()-- Wanted to read "F_SIZE_T" objects (size="F_SIZE_T"), read "F_SIZE_T".\n",
              toread, size, written);
      assert(errno == 0);
    }
  }

 finish:
  //  Just annoys developers.  Stop it.
  //if (position != nobj)
  //  fprintf(stderr, "AS_UTL_safeRead()--  Short read; wanted "F_SIZE_T" objects, read "F_SIZE_T" instead.\n",
  //          nobj, position);
  return(position);
}



//  Ensure that directory 'dirname' exists.  Returns true if the
//  directory needed to be created, false if it already exists.
int
AS_UTL_mkdir(const char *dirname) {
  struct stat  st;

  errno = 0;
  stat(dirname, &st);
  if (errno == 0) {
    if (S_ISDIR(st.st_mode))
      return(0);
    fprintf(stderr, "AS_UTL_mkdir()--  ERROR!  '%s' is a file, and not a directory.\n", dirname);
    exit(1);
  }

  if (errno != ENOENT) {
    fprintf(stderr, "AS_UTL_mkdir()--  Couldn't stat '%s': %s\n", dirname, strerror(errno));
    exit(1);
  }

  errno = 0;
  mkdir(dirname, S_IRWXU | S_IRWXG | S_IRWXO);
  if ((errno) && (errno != EEXIST)) {
    fprintf(stderr, "AS_UTL_mkdir()--  Couldn't create directory '%s': %s\n", dirname, strerror(errno));
    exit(1);
  }

  return(1);
}

//  Remove a file, or do nothing if the file doesn't exist.  Returns true if the file
//  was deleted, false if the file never existsed.
int
AS_UTL_unlink(const char *filename) {

  if (AS_UTL_fileExists(filename, FALSE, FALSE) == 0)
    return(0);

  errno = 0;
  unlink(filename);
  if (errno) {
    fprintf(stderr, "AS_UTL_unlink()--  Failed to remove file '%s': %s\n", filename, strerror(errno));
    exit(1);
  }

  return(1);
}




//  Returns true if the named file/directory exists, and permissions
//  allow us to read and/or write.
//
int
AS_UTL_fileExists(const char *path,
                  int directory,
                  int readwrite) {
  struct stat  s;
  int          r;

  errno = 0;
  r = stat(path, &s);
  if (errno)
    return(0);

  if ((directory == 1) &&
      (readwrite == 0) &&
      (s.st_mode & S_IFDIR) &&
      (s.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)) &&
      (s.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
    return(1);

  if ((directory == 1) &&
      (readwrite == 1) &&
      (s.st_mode & S_IFDIR) &&
      (s.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)) &&
      (s.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) &&
      (s.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
    return(1);

  if ((directory == 0) &&
      (readwrite == 0) &&
      (s.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)))
    return(1);

  if ((directory == 0) &&
      (readwrite == 1) &&
      (s.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)) &&
      (s.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
    return(1);

  return(0);
}





off_t
AS_UTL_sizeOfFile(const char *path) {
  struct stat  s;
  int          r;
  off_t        size = 0;

  errno = 0;
  r = stat(path, &s);
  if (errno) {
    fprintf(stderr, "Failed to stat() file '%s': %s\n", path, strerror(errno));
    exit(1);
  }

  //  gzipped files contain a file contents list, which we can
  //  use to get the uncompressed size.
  //
  //  gzip -l <file>
  //  compressed        uncompressed  ratio uncompressed_name
  //       14444               71680  79.9% up.tar
  //
  //  bzipped files have no contents and we just guess.

  if        (strcasecmp(path+strlen(path)-3, ".gz") == 0) {
    char   cmd[256];
    FILE  *F;

    sprintf(cmd, "gzip -l %s", path);
    F = popen(cmd, "r");
    fscanf(F, " %*s %*s %*s %*s ");
    fscanf(F, " %*d "F_OFF_T" %*s %*s ", &size);
    pclose(F);
  } else if (strcasecmp(path+strlen(path)-4, ".bz2") == 0) {
    size = s.st_size * 14 / 10;
  } else {
    size = s.st_size;
  }

  return(size);
}




off_t
AS_UTL_ftell(FILE *stream) {
  off_t  pos = 0;
  errno = 0;
  pos = ftello(stream);
  if ((errno == ESPIPE) || (errno == EBADF))
    //  Not a seekable stream.  Return some goofy big number.
    return(((off_t)1) < 42);
  if (errno)
    fprintf(stderr, "AS_UTL_ftell()--  Failed with %s.\n", strerror(errno));
  assert(errno == 0);
  return(pos);
}



void
AS_UTL_fseek(FILE *stream, off_t offset, int whence) {
  off_t   beginpos = AS_UTL_ftell(stream);

  //  If the stream is already at the correct position, just return.
  //
  //  Unless we're on FreeBSD.  For unknown reasons, FreeBSD fails
  //  updating the gkpStore with mate links.  It seems to misplace the
  //  file pointer, and ends up writing the record to the wrong
  //  location.  ftell() is returning the correct current location,
  //  and so AS_PER_genericStore doesn't seek() and just writes to the
  //  current position.  At the end of the write, we're off by 4096
  //  bytes.
  //
  //  LINK 498318175,1538 <-> 498318174,1537
  //  AS_UTL_fseek()--  seek to 159904 (whence=0); already there
  //  safeWrite()-- write nobj=1x104 = 104 bytes at position 159904
  //  safeWrite()-- wrote nobj=1x104 = 104 bytes position now 164000
  //  safeWrite()-- EXPECTED 160008, ended up at 164000
  //
#if !defined __FreeBSD__ && !defined __osf__ && !defined __APPLE__
  if ((whence == SEEK_SET) && (beginpos == offset)) {
#ifdef DEBUG_SEEK
    //  This isn't terribly informative, and adds a lot of clutter.
    //fprintf(stderr, "AS_UTL_fseek()--  seek to "F_OFF_T" (whence=%d); already there\n", offset, whence);
#endif
    return;
  }
#endif  //  __FreeBSD__

  errno = 0;
  fseeko(stream, offset, whence);
  if (errno) {
    fprintf(stderr, "AS_UTL_fseek()--  Failed with %s.\n", strerror(errno));
    assert(errno == 0);
  }

#ifdef DEBUG_SEEK
  fprintf(stderr, "AS_UTL_fseek()--  seek to "F_OFF_T" (requested "F_OFF_T", whence=%d) from "F_OFF_T"\n",
          AS_UTL_ftell(stream), offset, whence, beginpos);
#endif

  if (whence == SEEK_SET)
    assert(AS_UTL_ftell(stream) == offset);
}



compressedFileReader::compressedFileReader(const char *filename) {
  char  cmd[FILENAME_MAX * 2];
  int32 len = 0;

  _file = NULL;
  _pipe = false;
  _stdi = false;

  if (filename != NULL)
    len = strlen(filename);

  if ((len > 0) && (AS_UTL_fileExists(filename, FALSE, FALSE) == FALSE))
    fprintf(stderr, "ERROR:  Failed to open input file '%s': %s\n", filename, strerror(errno)), exit(1);

  errno = 0;

  if        ((len > 3) && (strcasecmp(filename + len - 3, ".gz") == 0)) {
    sprintf(cmd, "gzip -dc %s", filename);
    _file = popen(cmd, "r");
    _pipe = true;

  } else if ((len > 4) && (strcasecmp(filename + len - 4, ".bz2") == 0)) {
    sprintf(cmd, "bzip2 -dc %s", filename);
    _file = popen(cmd, "r");
    _pipe = true;

  } else if ((len > 3) && (strcasecmp(filename + len - 3, ".xz") == 0)) {
    sprintf(cmd, "xz -dc %s", filename);
    _file = popen(cmd, "r");
    _pipe = true;

  } else if ((len == 0) || (strcmp(filename, "-") == 0)) {
    _file = stdin;
    _stdi = 1;

  } else {
    _file = fopen(filename, "r");
    _pipe = false;
  }

  if (errno)
    fprintf(stderr, "ERROR:  Failed to open input file '%s': %s\n", filename, strerror(errno)), exit(1);
}


compressedFileReader::~compressedFileReader() {

  if (_file == NULL)
    return;

  if (_stdi)
    return;

  if (_pipe)
    pclose(_file);
  else
    fclose(_file);
}



compressedFileWriter::compressedFileWriter(const char *filename, int32 level) {
  char   cmd[FILENAME_MAX * 2];
  int32  len = 0;

  _file = NULL;
  _pipe = false;
  _stdi = false;

  if (filename != NULL)
    len = strlen(filename);

  errno = 0;

  if        ((len > 3) && (strcasecmp(filename + len - 3, ".gz") == 0)) {
    sprintf(cmd, "gzip -%dc > %s", level, filename);
    _file = popen(cmd, "w");
    _pipe = true;

  } else if ((len > 4) && (strcasecmp(filename + len - 4, ".bz2") == 0)) {
    sprintf(cmd, "bzip2 -%dc > %s", level, filename);
    _file = popen(cmd, "w");
    _pipe = true;

  } else if ((len > 3) && (strcasecmp(filename + len - 3, ".xz") == 0)) {
    sprintf(cmd, "xz -%dc > %s", level, filename);
    _file = popen(cmd, "w");
    _pipe = true;

  } else if ((len == 0) || (strcmp(filename, "-") == 0)) {
    _file = stdout;
    _stdi = 1;

  } else {
    _file = fopen(filename, "w");
    _pipe = false;
  }

  if (errno)
    fprintf(stderr, "ERROR:  Failed to open output file '%s': %s\n", filename, strerror(errno)), exit(1);
}


compressedFileWriter::~compressedFileWriter() {

  if (_file == NULL)
    return;

  if (_stdi)
    return;

  if (_pipe)
    pclose(_file);
  else
    fclose(_file);
}
