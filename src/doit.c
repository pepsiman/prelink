/* Copyright (C) 2001 Red Hat, Inc.
   Written by Jakub Jelinek <jakub@redhat.com>, 2001.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <alloca.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "prelinktab.h"

struct collect_ents
  {
    struct prelink_entry **ents;
    int nents;
  };

static int
find_ents (void **p, void *info)
{
  struct collect_ents *l = (struct collect_ents *) info;
  struct prelink_entry *e = * (struct prelink_entry **) p;

  if ((e->type == ET_DYN && e->done == 1)
      || (e->type == ET_EXEC && e->done == 0 && ! libs_only))
    l->ents[l->nents++] = e;

  return 1;
}

int
prelink_ent (struct prelink_entry *ent)
{
  int i;
  DSO *dso;
  char *filename;
  struct stat64 st;

  for (i = 0; i < ent->ndepends; ++i)
    if (ent->depends[i]->done == 1 && prelink_ent (ent->depends[i]))
      return 1;

  for (i = 0; i < ent->ndepends; ++i)
    if (ent->depends[i]->done != 2)
      {
	ent->done = 0;
	if (verbose)
	  error (0, 0, "Could not prelink %s because its dependency %s could not be prelinked",
		 ent->filename, ent->depends[i]->filename);
	return 0;
      }

  filename = canonicalize_file_name (ent->filename);
  if (filename == NULL)
    {
      error (0, errno, "Could not canonicalize filename %s", ent->filename);
      ent->done = 0;
      return 0;
    }

  if (verbose)
    {
      if (dry_run)
        printf ("Would prelink %s\n", filename);
      else
	printf ("Prelinking %s\n", filename);
    }

  dso = open_dso (filename);
  if (dso == NULL)
    goto error_out;

  if (fstat64 (dso->fd, &st) < 0)
    {
      error (0, errno, "%s changed during prelinking", ent->filename);
      goto error_out;
    }

  if (st.st_dev != ent->dev || st.st_ino != ent->ino)
    {
      error (0, 0, "%s changed during prelinking", ent->filename);
      goto error_out;
    }

  if (dry_run)
    {
      close_dso (dso);
      ent->done = 2;
      return 0;
    }

  if (prelink_prepare (dso))
    goto error_out;
  if (ent->type == ET_DYN && relocate_dso (dso, ent->base))
    goto error_out;
  if (prelink (dso, ent))
    goto error_out;
  if (update_dso (dso))
    {
      dso = NULL;
      goto error_out;
    }
  ent->done = 2;
  free (filename);
  return 0;

error_out:
  ent->done = 0;
  free (filename);
  if (dso)
    close_dso (dso);
  return 0;
}

int
prelink_all (void)
{
  struct collect_ents l;
  int i;

  l.ents =
    (struct prelink_entry **) alloca (prelink_entry_count
				      * sizeof (struct prelink_entry *));
  l.nents = 0;
  htab_traverse (prelink_filename_htab, find_ents, &l);

  for (i = 0; i < l.nents; ++i)
    if (l.ents[i]->done == 1 && prelink_ent (l.ents[i]))
      return 1;
    else if (l.ents[i]->done == 0 && l.ents[i]->type == ET_EXEC
	     && prelink_ent (l.ents[i]))
      return 1;

  return 0;
}