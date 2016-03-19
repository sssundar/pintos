#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/*! Creates a directory with space for ENTRY_CNT entries in the
    given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt,
		const char *name, block_sector_t parent) {
	ASSERT(entry_cnt > 0);

	// TODO the 0 length needs to change after file extension is implemented
	return inode_create(sector, 0, true, name, parent);

	/*
    return inode_create(sector, entry_cnt * sizeof(struct dir_entry),
    		true, name, parent);
    */
}

/*! Opens and returns the directory for the given INODE, of which
    it takes ownership.  Returns a null pointer on failure. */
struct dir * dir_open(struct inode *inode) {
    struct dir *dir = calloc(1, sizeof(*dir));
    if (inode != NULL && dir != NULL) {
        dir->inode = inode;
        dir->pos = 0;
        return dir;
    }
    else {
        inode_close(inode);
        free(dir);
        return NULL; 
    }
}

/*! Opens the root directory and returns a directory for it.
    Return true if successful, false on failure. */
struct dir * dir_open_root(void) {
    return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir * dir_reopen(struct dir *dir) {
    return dir_open(inode_reopen(dir->inode));
}

/*! Destroys DIR and frees associated resources. */
void dir_close(struct dir *dir) {
    if (dir != NULL) {
        inode_close(dir->inode);
        free(dir);
    }
}

/*! Returns the inode encapsulated by DIR. */
struct inode * dir_get_inode(struct dir *dir) {
    return dir->inode;
}

/*! Searches DIR for a file with the given NAME.
    If successful, returns true, sets *EP to the directory entry
    if EP is non-null, and sets *OFSP to the byte offset of the
    directory entry if OFSP is non-null.
    otherwise, returns false and ignores EP and OFSP. */
/*
static bool lookup(const struct dir *dir, const char *name,
                   struct dir_entry *ep, off_t *ofsp) {
    struct dir_entry e;
    size_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (e.in_use && !strcmp(name, e.name)) {
            if (ep != NULL)
                *ep = e;
            if (ofsp != NULL)
                *ofsp = ofs;
            return true;
        }
    }
    return false;
}
*/

/*! Searches DIR for a file with the given NAME and returns true if one exists,
    false otherwise.  On success, sets *INODE to an inode for the file,
    otherwise to a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir *dir, const char *name, struct inode **inode) {
    // struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);
    
    block_sector_t sect = inode_find_matching_dir_entry(dir->inode, name);
    if (sect == BOGUS_SECTOR) {        
    	*inode = NULL;
    	return false;
    }
    
    *inode = inode_open(sect);
    if (*inode == NULL)
    	 return false;
    return true;

    /*
    if (lookup(dir, name, &e, NULL))
        *inode = inode_open(e.inode_sector);
    else
        *inode = NULL;

    return *inode != NULL;
    */
}

/*! Adds a file named NAME to DIR, which must not already contain a file by
    that name.  The file's inode is in sector INODE_SECTOR.
    Returns true if successful, false on failure.
    Fails if NAME is invalid (i.e. too long) or a disk or memory
    error occurs. */
bool dir_add(struct dir *dir, const char *name, block_sector_t inode_sector) {
    // struct dir_entry e;
    // off_t ofs;
    bool success = false;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    // Check NAME for validity.
    if (*name == '\0' || strlen(name) > NAME_MAX)
        return false;

    // Check that NAME is not in use.
    if(inode_find_matching_dir_entry(dir->inode, name) != BOGUS_SECTOR)
    	goto done;

    // If we got here then the NAME is valid and unused in this directory.
    // Add it to the directory by putting its sector into the directory's
    // entries array.
    int idx = inode_get_first_open_directory_slot(dir->inode);
    if (idx == -1) {
    	PANIC("Not enough room in the inode for a new directory entry.");
    	NOT_REACHED();
    }
    // Add the given filename to the inode's sector in case it's not already
    // there.
    dir->inode->dir_contents[idx] = inode_sector;
    success = true;

    /*
    // Check that NAME is not in use.
    if (lookup(dir, name, NULL, NULL))
        goto done;

    // Set OFS to offset of free slot. If there are no free slots, then it will
    // be set to the current end-of-file. inode_read_at() will only return a
    // short read at end of file. Otherwise, we'd need to verify that we didn't
    // get a short read due to something intermittent such as low memory.
    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (!e.in_use)
            break;
    }

    // Write slot.
    e.in_use = true;
    strlcpy(e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    success = inode_write_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
	*/

done:
    return success;
}

/*! Removes any entry for NAME in DIR.  Returns true if successful, false on
    failure, which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name) {
    // struct dir_entry e;
    struct inode *inode = NULL;
    bool success = false;
    // off_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    // Check that NAME is not in use.
    block_sector_t sect;
    int idx;
    inode_find_matching_idx_and_sector(dir->inode, name, &sect, &idx);
	if(sect == BOGUS_SECTOR)
		goto done;

	// Erase the entry for the entries array of the directory's inode.
	if (idx != -1) {
		dir->inode->dir_contents[idx] = BOGUS_SECTOR;
	}

	// Open inode, remove it.
	inode = inode_open(sect);
	if (inode == NULL)
		goto done;
	inode_remove(inode);
	success = true;

    /*
    // Find directory entry.
    if (!lookup(dir, name, &e, &ofs))
        goto done;

    // Open inode.
    inode = inode_open(e.inode_sector);
    if (inode == NULL)
        goto done;

    // Erase directory entry.
    e.in_use = false;
    if (inode_write_at(dir->inode, &e, sizeof(e), ofs) != sizeof(e))
        goto done;

    // Remove inode.
    inode_remove(inode);
    success = true;
	*/

done:
    inode_close(inode);
    return success;
}

/*! Reads the next directory entry in DIR and stores the name in NAME.  Returns
    true if successful, false if the directory contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1]) {
	bool success = false;

    if (dir->pos < MAX_DIR_ENTRIES && dir->pos >=0) {
    	struct inode *entry_inode =
    			inode_open(dir->inode->dir_contents[dir->pos]);
    	if (entry_inode == NULL) {
    		PANIC("Couldn't open entry's inode.");
    		NOT_REACHED();
    	}

    	strlcpy(name, entry_inode->filename, NAME_MAX + 1);
    	success = true;
    }

    dir->pos = (dir->pos + 1) % MAX_DIR_ENTRIES;
    return success;
}

/*! Return the sector in which the given relative- or absolute-named file
    lives. Returns NULL if can't find the directory. User must close the
    inode returned. Stores the parent directory's inode pointer into PARENT
    and stores the filename into FILENAME. */
struct inode *dir_get_inode_from_path(const char *path,
		struct inode **parent, char *filename) {
	char *last_slash = strrchr(path, '/');

	// If we end with slash like "/a/b/c/" then recurse without trailing slash
	if (last_slash == path + strlen(path) - 1) {
		char *path_copy = (char *) malloc (sizeof (char) * strlen(path));
		strlcpy(path_copy, path, strlen(path));
		struct inode *rtn =
				dir_get_inode_from_path(path_copy, parent, filename);
		free (path_copy);
		return rtn;
	}

	//printf("  --> path = \"%s\"\n", path);

	// For paths of the form "..", ".", or "filename" just return the
	// the corresponding inode.
	if (last_slash == NULL) {
		block_sector_t files_sect = inode_find_matching_dir_entry(
				thread_current()->cwd.inode, path);

		//printf("  --> matching sector = %u\n", files_sect);

		struct inode *rtn = NULL;
		if (files_sect != BOGUS_SECTOR) {
			rtn = inode_open(files_sect);
			if (rtn == NULL) {
				PANIC("Memory allocation failed in inode_open");
				NOT_REACHED();
			}
		}
		*parent = thread_current()->cwd.inode;
		strlcpy(filename, path, NAME_MAX + 1);

		//printf("  --> leaving with filename=  \"%s\", parent = %p, parent name = \"%s\"\n",
		//					filename, *parent, (*parent)->filename);

		return rtn;
	}

	// Now if the filename starts with a forward slash then assume it's an
	// absolute path, otherwise it's a relative one. If it's an absolute path
	// then the starting directory for our search for the inode is
	// the root directory. Otherwise it's the current working directory.
	char *path_ptr = (char *) path;
	block_sector_t curr_dir_sector;
	if (path[0] == '/')
		curr_dir_sector = ROOT_DIR_SECTOR;
	else
		curr_dir_sector = thread_current()->cwd.inode->sector;

	//printf(" --> curr_dir_sector (before loop): %u\n", curr_dir_sector);

	// Iterate over each directory in the path.
	struct inode *tmpinode = NULL;
	while (path_ptr < last_slash) {
		if (tmpinode != NULL)
			inode_close(tmpinode);

		//printf(" --> path_ptr (top of loop): \"%s\"\n", path_ptr);

		// Find the current directory name from the path.
		char curr_dir_name[NAME_MAX + 1];
		char *first_slash = strchr(path_ptr, '/');
		strlcpy(curr_dir_name, path_ptr, first_slash - path_ptr + 1);

		//printf(" --> curr_dir_name: \"%s\"\n", curr_dir_name);


		// Get the directory's sector from disk or the cache.
		tmpinode = inode_open(curr_dir_sector);
		if (tmpinode == NULL) {
			PANIC ("Couldn't alloc memory when opening inode.");
			NOT_REACHED();
		}
		ASSERT(tmpinode->is_dir);

		// Iterate over the sectors in the directory's entries list, looking
		// for one with a matching name.
		if (strcmp(curr_dir_name, "..") == 0) {
			curr_dir_sector = tmpinode->parent_dir;
		}
		else if (strcmp(curr_dir_name, ".") == 0) {
			// curr_dir_sector says the same.
		}
		else {
			block_sector_t tmp_dir_sector =
					inode_find_matching_dir_entry(tmpinode, curr_dir_name);
			// If we couldn't find the requested file then return NULL.
			if (tmp_dir_sector == BOGUS_SECTOR) {
				*parent = NULL;
				strlcpy(filename, "", 1);
				return NULL;
			}
			curr_dir_sector = tmp_dir_sector;
		}

		//printf(" --> curr_dir_sector (end loop): %u\n", curr_dir_sector);

		path_ptr = first_slash + 1;
	}

	//printf("--> path_ptr (after loop): \"%s\"\n", path_ptr);

	// Otherwise we need to get the inode by using the end of the path
	// and plugging it into find_matching again.
	char name_at_end[NAME_MAX + 1];
	strlcpy(name_at_end, path_ptr, (path + strlen(path)) - path_ptr + 1);
	strlcpy(filename, name_at_end, NAME_MAX + 1);
	block_sector_t fsect =
			inode_find_matching_dir_entry(tmpinode, name_at_end);

	//printf(" --> fsect: %u\n", fsect);

	ASSERT(tmpinode != NULL);
	inode_close(tmpinode);
	*parent = inode_open(curr_dir_sector);


	//printf("--> leaving with filename=  \"%s\", parent = %p, parent name = \"%s\"\n",
	//					filename, *parent, (*parent)->filename);

	if (fsect == BOGUS_SECTOR)
		return NULL;
	struct inode *rtn = inode_open(fsect);
	if (rtn == NULL) {
		PANIC ("Couldn't alloc memory when opening inode.");
		NOT_REACHED();
	}
	return rtn;
}
