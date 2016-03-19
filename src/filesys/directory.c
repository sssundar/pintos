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

done:
    return success;
}

/*! Removes any entry for NAME in DIR.  Returns true if successful, false on
    failure, which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name) {
    // struct dir_entry e;
    struct inode *inode = NULL;
    bool success = false;

    ASSERT(dir != NULL && dir->inode != NULL);
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

done:
    inode_close(inode);
    return success;
}

/*! Reads the next directory entry in DIR and stores the name in NAME.  Returns
    true if successful, false if the directory contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1]) {
	bool success = false;

	//printf("  --> in dir_readdir. dir = \"%s\"\n", dir->inode->filename);
	//printf("  --> pos is %d\n", dir->pos);

    while (dir->pos < MAX_DIR_ENTRIES && dir->pos >=0) {
    	if (dir->inode->dir_contents[dir->pos] == BOGUS_SECTOR) {
    		dir->pos++;
    		continue;
    	}
    	struct inode *entry_inode =
    			inode_open(dir->inode->dir_contents[dir->pos]);
    	if (entry_inode == NULL) {
    		PANIC("Couldn't open entry's inode.");
    		NOT_REACHED();
    	}
    	strlcpy(name, entry_inode->filename, NAME_MAX + 1);
    	success = true;
    }

    dir->pos++;
    return success;
}

/*! Return the sector in which the given relative- or absolute-named file
    lives. Returns NULL if can't find the directory. User must close the
    inode returned. Stores the parent directory's inode pointer into PARENT
    and stores the filename into FILENAME. */
struct inode *dir_get_inode_from_path(const char *path,
		struct inode **parent, char *filename) {
	char *last_slash = strrchr(path, '/');

	if (strcmp(path, "/") == 0) {
		struct inode *rtn = dir_open_root()->inode;
		*parent = NULL;
		strlcpy(filename, "/", 2);
		return rtn;
	}

	/* If we end with slash like "/a/b/c/" then recurse w/o trailing slash. */
	if (last_slash == path + strlen(path) - 1) {
		char *path_copy = (char *) malloc (sizeof (char) * strlen(path));
		strlcpy(path_copy, path, strlen(path));
		struct inode *rtn =
				dir_get_inode_from_path(path_copy, parent, filename);
		free (path_copy);
		return rtn;
	}

	//printf("  --> path = \"%s\"\n", path);

	/* For paths of the form "..", ".", "filename", or "/filename" just
	   return the the corresponding inode. */
	if (last_slash == NULL || last_slash == path) {
		char trim_path[NAME_MAX + 1];
		strlcpy(trim_path, path + (last_slash == path ? 1 : 0), NAME_MAX + 1);

		struct inode *tinode = NULL;
		if (last_slash != path) {
			tinode = inode_open(thread_current()->cwd_sect);
		}

		*parent = last_slash == path ? dir_open_root()->inode : tinode;
		block_sector_t files_sect = inode_find_matching_dir_entry(
				*parent, trim_path);

		//printf("    --> matching sector = %u\n", files_sect);

		struct inode *rtn = NULL;
		if (files_sect != BOGUS_SECTOR) {
			rtn = inode_open(files_sect);
			if (rtn == NULL) {
				PANIC("Memory allocation failed in inode_open");
				NOT_REACHED();
			}
		}
		strlcpy(filename, trim_path, NAME_MAX + 1);

		//printf("    --> leaving with filename=  \"%s\", psect = %u, parent name = \"%s\"\n",
		//					filename, (*parent)->sector, (*parent)->filename);

		return rtn;
	}

	/* Now if the filename starts with a forward slash then assume it's an
	   absolute path, otherwise it's a relative one. If it's an absolute path
	   then the starting directory for our search for the inode is
	   the root directory. Otherwise it's the current working directory. */
	char *path_ptr = (char *) path;
	block_sector_t curr_dir_sector;
	if (path[0] == '/') {
		curr_dir_sector = ROOT_DIR_SECTOR;
		path_ptr++;
	}
	else
		curr_dir_sector = thread_current()->cwd_sect;

	//printf("  --> curr_dir_sector (before loop): %u\n", curr_dir_sector);

	/* Iterate over each directory in the path. */
	struct inode *tmpinode = NULL;
	while (path_ptr < last_slash) {
		if (tmpinode != NULL)
			inode_close(tmpinode);

		//printf("    --> path_ptr (top of loop): \"%s\"\n", path_ptr);

		/* Find the current directory name from the path. */
		char curr_dir_name[NAME_MAX + 1];
		char *first_slash = strchr(path_ptr, '/');
		strlcpy(curr_dir_name, path_ptr, first_slash - path_ptr + 1);

		//printf("    --> curr_dir_name: \"%s\"\n", curr_dir_name);


		/* Get the directory's sector from disk or the cache. */
		tmpinode = inode_open(curr_dir_sector);
		if (tmpinode == NULL) {
			PANIC ("Couldn't alloc memory when opening inode.");
			NOT_REACHED();
		}
		ASSERT(tmpinode->is_dir);

		//printf("    --> info about tmpinode: sect=%u, fname=\"%s\", psect=%u\n",
		//		tmpinode->sector, tmpinode->filename, tmpinode->parent_dir);

		/* Iterate over the sectors in the directory's entries list, looking
		   for one with a matching name. */
		if (strcmp(curr_dir_name, "..") == 0) {
			curr_dir_sector = tmpinode->parent_dir;
		}
		else if (strcmp(curr_dir_name, ".") == 0) {
			// curr_dir_sector says the same.
		}
		else {
			block_sector_t tmp_dir_sector =
					inode_find_matching_dir_entry(tmpinode, curr_dir_name);
			/* If we couldn't find the requested file then return NULL. */
			if (tmp_dir_sector == BOGUS_SECTOR) {
				*parent = NULL;
				strlcpy(filename, "", 1);
				return NULL;
			}
			curr_dir_sector = tmp_dir_sector;
		}

		//printf("    --> curr_dir_sector (end loop): %u\n", curr_dir_sector);

		path_ptr = first_slash + 1;
	}

	//printf("  --> path_ptr (after loop): \"%s\"\n", path_ptr);

	/* Otherwise we need to get the inode by using the end of the path
	   and plugging it into find_matching again. */
	char name_at_end[NAME_MAX + 1];
	strlcpy(name_at_end, path_ptr, (path + strlen(path)) - path_ptr + 1);
	strlcpy(filename, name_at_end, NAME_MAX + 1);
	*parent = inode_open(curr_dir_sector);

	ASSERT(tmpinode != NULL);
	ASSERT(tmpinode->is_dir);
	block_sector_t fsect = BOGUS_SECTOR;
	if (*parent != NULL)
		fsect = inode_find_matching_dir_entry(*parent, name_at_end);

	//printf("  --> fsect: %u\n", fsect);

	//printf("  --> leaving with filename=  \"%s\", parent = %p, parent name = \"%s\"\n",
	//					filename, *parent, (*parent)->filename);

	inode_close(tmpinode);
	if (fsect == BOGUS_SECTOR)
		return NULL;
	struct inode *rtn = inode_open(fsect);
	if (rtn == NULL) {
		PANIC ("Couldn't alloc memory when opening inode.");
		NOT_REACHED();
	}
	return rtn;
}
