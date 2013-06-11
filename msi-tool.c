/* msi-tool.c -- Builds directory, component, file, and feature
   tables for an MSI file.

Copyright (C) 2013 Andrew Makousky

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

/*
   See `README.txt' for detailed tutorial on how to use `msi-tool' to
   help you build a Windows Installer package, along with general
   information for involking `msi-tool'.

   Originally, I wrote this code in a hurry to help me manually build
   a single proof-of-concept Windows Installer package.  However,
   because of the fact that I had to write a lot of code to get it
   done and the code I wrote could prove to be a useful tool, I
   decided to come back and polish up what I wrote.  Most of my
   changes so far are robustness and finalizing features that an
   end-user would expect from this tool.

   Currently, I am not yet finished with this code finalization
   process.  This is what work still needs to be done:

   * The feature description file should accept individual file names
     under feature specifications.

   * `msi-tool' should be able to parse in more than two `ls -R'
     files.

   * `msi-tool' should provide more facilities for build automation
     and require less editing of the generated output tables.

   Developers who plan on working on this code will find the following
   conceptual commentary helpful.

   `msi-tool' uses dynamically-allocated memory structures
   extensively.  Thus, memory management can get to become an issue.
   `msi-tool' primarily takes care of the memory management issue by
   an ownership model.  A memory block is owned by only one variable,
   and any other variables that point to that memory block are
   considered to be sharing it.  Thus, deallocation of memory is
   handled entirely through the owner variable.

   `msi-tool' also uses a dynamic array structure called `exparray'.
   Originally, I wrote the first version of `exparray' mostly as a
   mechanism to reduce the amount of typing I do when programming in
   pure C.  (Note that I only formally learned C++ programming, not C
   programming.)  However, when I learned about GLib, I later adapted
   `exparray' to be a mix of GArray-like constructs and the original
   `exparray' implementation.  `msi-tool' predates the time that I
   made improvements to my `exparray' implementation, so some of my
   older `exparray' programming styles, such as calling
   `free(exparray.d)' rather than `EA_DESTROY(exparray)' remain in
   this code.  All expandable memory structures throughout the code
   are represented as exparrays.

   Most of the memory ownership is handled by the table structures.
   The table structures are exparrays of pointers to
   dynamically-allocated C strings.  Any string put into the table
   structure is never changed; thus, it can be safely shared.  If you
   need more information about the contents of the table structures,
   you should look at the relevent Windows Platform SDK documentation.
   The major data structures `rootDir', `rootDirN', and `qsortFiles'
   share all of their strings.  The only dynamic memory they own is
   the dynamic memory necessary to represent their array and tree
   structures.

   Sometimes I will use xmalloc() and sprintf() together to create
   certain strings.  All of my code assumes that one character is one
   byte and integers and 32 bits in length.  Thus, the maximum number
   of bytes needed to store an integer converted to a string is 11
   (-2147483648).

   Sometimes, expandable arrays are used to hold C strings.  One
   common task is to append characters onto the end of such a string.
   This is sometimes achieved by first storing the null terminating
   character and inserting just before the null terminator.

   `msi-tool' works with stacks extensively.  Perhaps the code would
   be more readable if I defined a specific stack data structure and
   related functions, but for now, documentation of the common idioms
   will have to do.  Often times, I will have code that works with a
   stack of directory names (or something similar).  For example, I
   may parse the pathname `/usr/local/share' into the following array:
   { "usr", "local", "share" }.  Then I will need to change to a
   different directory.  Thus I will need to compare the new pathname
   to the previous pathname by iterating up the directory stack.  If
   at the point that the paths do not match, I will clear every
   subsequent directory from the stack and then build the new names on
   top of the existing ones.  Whenever I need to work with a similar
   hierarchical structure in the code, I take an similar approach,
   reusing this concept.  If you see some code that you don't quite
   understand at first glance and you notice it has something to do
   with levels, think about how it might be used for maintaining a
   stack like I have explained.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "xmalloc.h"
#define ea_malloc xmalloc
#define ea_realloc xrealloc
#define ea_free xfree
#include "exparray.h"
#include "bool.h"

/* Define necessary types before including local headers.  */
EA_TYPE(char);

/* Local includes */
#include "colon-parser.h"

/* Type definitions */

typedef char* char_ptr;
typedef struct DirTree_t DirTree;
typedef struct FileIndex_t FileIndex;

EA_TYPE(char_ptr);
EA_TYPE(unsigned);
EA_TYPE(DirTree);
EA_TYPE(FileIndex);

/* Structure definitions */

struct DirTree_t
{
	char* name;
	unsigned tableRow;
	char* dirKey;
	char* component;
	unsigned compRefCount; /* currently unnecessary */
	/* Were components created for separate groupings of files within
	   the same directory?  */
	bool fileComps;
	DirTree_array children;
	/* Indices into fileTable, currently unnecessary */
	unsigned_array fileIdcs;
};

struct FileIndex_t
{
	char* name;
	unsigned tableIndex;
};

/* Container helper functions */

int FileIndex_qsort(const void* e1, const void* e2)
{
	return strcmp(((FileIndex*)e1)->name, ((FileIndex*)e2)->name);
}

/* Tables */
char_ptr_array dirTable;		const unsigned dirCols = 3;
char_ptr_array compTable;		const unsigned compCols = 6;
char_ptr_array fileTable;		const unsigned fileCols = 8;
char_ptr_array featureTable;	const unsigned featureCols = 8;
char_ptr_array featCompTable;	const unsigned featCompCols = 2;

/* Global parameter variables */
char* idPrefix = "";
bool renameFiles = false;
char* progDirName = "";
char* progDirID = NULL;

/* Edgy global variables */
FILE* uuidFP = NULL;
DirTree* curDir;
DirTree rootDir;
unsigned curRoot; /* The current non-first root directory */
DirTree_array rootDirN;
/* `rootNameN' owns the names of all root directories other than the
   first.  */
char_ptr_array rootNameN;
FileIndex_array qsortFiles; /* currently unnecessary */

/* Parser callback state variables */
char_ptr_array dirStack;
/* Associations between directories on the directory stack and indices
   into the `Directory' table.  The indices refer to the start of the
   row.  */
unsigned_array dirStkAssoc;
bool firstList;
bool addedComponent;
char* dirID;
char_ptr_array featStack;
unsigned_array featStkAssoc;
bool reusedComponent;
DirTree* lastDir = NULL;

/* Parser callback functions */
int LSRAddBody(unsigned curLevel, char_array* colonLabel);
int LSRRemoveLevels(unsigned testLevel);
int LSRAddItem(char_array* itemName);
int FeatAddBody(unsigned curLevel, char_array* colonLabel);
int FeatRemoveLevels(unsigned testLevel);
int FeatAddItem(char_array* itemName);

/* Helper functions */
void DisplayCmdHelp();
void GenerateTables();
char* GetUuid();
unsigned FindFile(FileIndex_array* database, char* filename,
				  unsigned begin, unsigned end);
DirTree* FindAnyDirTree(char* path);
DirTree* FindDirTree(DirTree* rootDir, char* path);
void AddFeatComps(char_ptr_array* featCompTable, char* featureID,
				  DirTree* curDir);
void FreeDirTree(DirTree* dir);

int main(int argc, char* argv[])
{
	int retval = 0;
	char_ptr_array lsrFiles;
	FILE* fp;

	/* Initialization */
	EA_INIT(char_ptr, lsrFiles, 16);
	EA_INIT(char_ptr, dirTable, 16);
	EA_INIT(char_ptr, compTable, 16);
	EA_INIT(char_ptr, fileTable, 16);
	EA_INIT(char_ptr, featureTable, 16);
	EA_INIT(char_ptr, featCompTable, 16);

	EA_INIT(char_ptr, dirStack, 16);
	EA_INIT(unsigned, dirStkAssoc, 16);

	curDir = &rootDir;
	EA_INIT(DirTree, rootDirN, 16);
	EA_INIT(char_ptr, rootNameN, 16);
	EA_INIT(FileIndex, qsortFiles, 16);

	EA_INIT(char_ptr, featStack, 16);
	EA_INIT(unsigned, featStkAssoc, 16);

	/* Process the command line.  */
	if (argc == 1)
	{
		DisplayCmdHelp();
		retval = 0; goto cleanup;
	}
	{
		unsigned i;
		for (i = 1; i < argc; i++)
		{
			char* cmdArg = argv[i];
			if (cmdArg[0] == '-')
			{
				switch (cmdArg[1])
				{
				case 'p':
					idPrefix = &cmdArg[2];
					break;
				case 'r':
					renameFiles = true;
					break;
				case 'd':
					progDirName = &cmdArg[2];
					break;
				default:
					fprintf(stderr, "Unknown command-line option: %s\n",
							cmdArg);
					retval = 1; goto cleanup;
				}
			}
			else
				EA_APPEND(lsrFiles, argv[i]);
		}
	}

	if (progDirName[0] == '\0')
		fputs("Missing `-d' command-line option.\n", stderr);
	if (lsrFiles.len == 0)
		fputs("Missing directory listing file name(s).\n", stderr);
	if (progDirName[0] == '\0' || lsrFiles.len == 0)
	{ retval = 1; goto cleanup; }

	{
		char* barPos;
		unsigned shortNameLen;
		unsigned i;
		barPos = strchr(progDirName, (int)'|');
		if (barPos == NULL)
		{
			fputs("ERROR: Incorrect formatting in application "
				  "folder name.\n", stderr);
			retval = 1; goto cleanup;
		}
		shortNameLen = barPos - progDirName;
		progDirID = (char*)xmalloc(shortNameLen + 3 + 1);
		strncpy(progDirID, progDirName, shortNameLen);
		progDirID[shortNameLen] = '\0';
		for (i = 0; i < shortNameLen; i++)
			progDirID[i] = (char)toupper((char)progDirID[i]);
		strcat(progDirID, "DIR");
	}

	/* Open the uuid file.  */
	uuidFP = fopen("uuids.txt", "r");
	if (uuidFP == NULL)
	{
		fputs("ERROR: Could not open file: uuids.txt\n", stderr);
		retval = 1; goto cleanup;
	}

	/* Parse the first ls -R listing.  */
	firstList = true;
	fp = fopen(lsrFiles.d[0], "r");
	if (fp == NULL)
	{
		fputs("ERROR: Could not open file: ls-r.txt\n", stderr);
		retval = 1; goto cleanup;
	}
	retval = ParseLSRFile(fp, LSRAddBody, LSRRemoveLevels, LSRAddItem);
	fclose(fp);
	if (!retval)
	{ retval = 1; goto cleanup; }

	/* Parse all the other ls -R listings.  */
	EA_SET_SIZE(rootDirN, lsrFiles.len - 1);
	for (curRoot = 0; curRoot < lsrFiles.len - 1; curRoot++)
	{
		/* Clear the directory stack.  */
		{
			unsigned i;
			for (i = 0; i < dirStack.len; i++)
			{
				xfree(dirStack.d[i]);
			}
		}
		EA_SET_SIZE(dirStack, 0);
		EA_SET_SIZE(dirStkAssoc, 0);

		curDir = &rootDirN.d[curRoot];

		/* Parse another ls -R listing.  */
		firstList = false;
		fp = fopen(lsrFiles.d[curRoot+1], "r");
		if (fp == NULL)
		{
			fprintf(stderr, "ERROR: Could not open file: %s\n",
					lsrFiles.d[curRoot+1]);
			retval = 1; goto cleanup;
		}
		retval = ParseLSRFile(fp, LSRAddBody, LSRRemoveLevels, LSRAddItem);
		fclose(fp);
		if (!retval)
		{ retval = 1; goto cleanup; }
	}

	/* Quick-sort a file lookup array.  */
	{
		unsigned i;
		for (i = 0; i < fileTable.len; i += fileCols)
		{
			char* longName;
			longName = strchr(fileTable.d[i+2], (int)'|') + 1;
			qsortFiles.d[i/fileCols].name = longName;
			qsortFiles.d[i/fileCols].tableIndex = i / fileCols;
			EA_ADD(qsortFiles);
		}
		qsort(qsortFiles.d, qsortFiles.len, sizeof(FileIndex),
			  FileIndex_qsort);
	}

	/* Open the feature file.  */
	/* The feature file contains a list of features, and with each
	   feature there is an associated list of files and possibly
	   directories.  Features can contain sub-features.  If a
	   directory is specified that does not map to a component,
	   the component inside the directory is picked.  */
	fp = fopen("features.txt", "r");
	if (fp == NULL)
	{
		fputs("ERROR: Could not open file: features.txt\n", stderr);
		retval = 1; goto cleanup;
	}
	retval = ParseLSRFile(fp, FeatAddBody, FeatRemoveLevels, FeatAddItem);
	fclose(fp);
	fclose(uuidFP); uuidFP = NULL;
	if (!retval)
	{ retval = 1; goto cleanup; }

	GenerateTables();
	retval = 0;

cleanup:
	{
		unsigned i;
		if (uuidFP != NULL)
			fclose(uuidFP);
		xfree(lsrFiles.d);
		xfree(progDirID);
		for (i = 0; i < dirTable.len; i += dirCols)
		{
			xfree(dirTable.d[i]);
			xfree(dirTable.d[i+2]);
		}
		xfree(dirTable.d);
		for (i = 0; i < compTable.len; i += compCols)
		{
			xfree(compTable.d[i]);
			xfree(compTable.d[i+1]);
		}
		xfree(compTable.d);
		for (i = 0; i < fileTable.len; i += fileCols)
		{
			xfree(fileTable.d[i]);
			xfree(fileTable.d[i+2]);
			xfree(fileTable.d[i+3]);
			xfree(fileTable.d[i+7]);
		}
		xfree(fileTable.d);
		for (i = 0; i < featureTable.len; i+= featureCols)
		{
			xfree(featureTable.d[i]);
			xfree(featureTable.d[i+2]);
			xfree(featureTable.d[i+4]);
		}
		xfree(featureTable.d);
		xfree(featCompTable.d);
		for (i = 0; i < dirStack.len; i++)
		{
			xfree(dirStack.d[i]);
		}
		xfree(dirStack.d);
		xfree(dirStkAssoc.d);
		FreeDirTree(&rootDir);
		for (i = 0; i < rootDirN.len; i++)
		{
			FreeDirTree(&rootDirN.d[i]);
			xfree(rootNameN.d[i]);
		}
		xfree(rootDirN.d);
		xfree(rootNameN.d);
		xfree(qsortFiles.d);
		for (i = 0; i < featStack.len; i++)
			xfree(featStack.d[i]);
		xfree(featStack.d);
		xfree(featStkAssoc.d);
	}

	return retval;
}

void DisplayCmdHelp()
{
	puts(
"Ussage:\n\
msi-tool [-pPREFIX] [-r] -dPROGFILES-DIRNAME LSR-FILE1 LSR-FILE2 ...\n\
\n\
msi-tool reads in directory listing files, a feature specification\n\
file, and a UUID file and generates corresponding tables for a Windows\n\
Installer.  The feature specification file must be named\n\
\"features.txt\" and the UUID file must be named \"uuids.txt\".  Directory\n\
listing files are specified on the command line.\n\
\n\
Options:\n\
\n\
  -pPREFIX       A prefix to add to generated identifiers.  Optional.\n\
\n\
  -r             Indicates that msi-tool should rename and move files\n\
                 to prepare for creating an embedded cabinet file.\n\
                 Optional.\n\
\n\
  -dPROGFILES-DIRNAME  The name of the application's directory that will\n\
                       be located within the Program Files folder.\n\
                       This option should take the form\n\
                       `shrtname|long-long-name'.");
}

void GenerateTables()
{
	FILE* fp;
	unsigned i;

	fp = fopen("Directory.idt", "w");
	fputs(
		"Directory\tDirectory_Parent\tDefaultDir\n"
		"s72\tS72\tl255\n"
		"Directory\tDirectory\n", fp);
	fputs("TARGETDIR\t\tSourceDir\n", fp);
	fputs("ProgramFilesFolder\tTARGETDIR\t.\n", fp);
	fprintf(fp, "%s\tProgramFilesFolder\t%s\n", progDirID, progDirName);
	fprintf(fp, "%s\t%s\t.\n", dirTable.d[0], dirTable.d[1]);
	for (i = dirCols; i < dirTable.len; i += dirCols)
	{
		unsigned j;
		fputs(dirTable.d[i], fp);
		for (j = 1; j < dirCols; j++)
		{
			fputs("\t", fp);
			fputs(dirTable.d[i+j], fp);
		}
		fputs("\n", fp);
	}
	fclose(fp);
	fp = fopen("Component.idt", "w");
	fputs(
		"Component\tComponentId\tDirectory_\tAttributes\tCondition\tKeyPath\n"
		"s72\tS38\ts72\ti2\tS255\tS72\n"
		"Component\tComponent\n", fp);
	for (i = 0; i < compTable.len; i += compCols)
	{
		unsigned j;
		fputs(compTable.d[i], fp);
		for (j = 1; j < compCols; j++)
		{
			fputs("\t", fp);
			fputs(compTable.d[i+j], fp);
		}
		fputs("\n", fp);
	}
	fclose(fp);
	fp = fopen("File.idt", "w");
	fputs(
		"File\tComponent_\tFileName\tFileSize\tVersion\tLanguage\t"
		  "Attributes\tSequence\n"
		"s72\ts72\tl255\ti4\tS72\tS20\tI2\ti2\n"
		"File\tFile\n", fp);
	for (i = 0; i < fileTable.len; i += fileCols)
	{
		unsigned j;
		fputs(fileTable.d[i], fp);
		for (j = 1; j < fileCols; j++)
		{
			fputs("\t", fp);
			fputs(fileTable.d[i+j], fp);
		}
		fputs("\n", fp);
	}
	fclose(fp);
	fp = fopen("Feature.idt", "w");
	fputs(
		"Feature\tFeature_Parent\tTitle\tDescription\tDisplay\tLevel\t"
		  "Directory_\tAttributes\n"
		"s38\tS38\tL64\tL255\tI2\ti2\tS72\ti2\n"
		"Feature\tFeature\n", fp);
	for (i = 0; i < featureTable.len; i += featureCols)
	{
		unsigned j;
		fputs(featureTable.d[i], fp);
		for (j = 1; j < featureCols; j++)
		{
			fputs("\t", fp);
			fputs(featureTable.d[i+j], fp);
		}
		fputs("\n", fp);
	}
	fclose(fp);
	fp = fopen("FeatureComponents.idt", "w");
	fputs(
		"Feature_\tComponent_\n"
		"s38\ts72\n"
		"FeatureComponents\tFeature_\tComponent_\n", fp);
	for (i = 0; i < featCompTable.len; i += featCompCols)
	{
		unsigned j;
		fputs(featCompTable.d[i], fp);
		for (j = 1; j < featCompCols; j++)
		{
			fputs("\t", fp);
			fputs(featCompTable.d[i+j], fp);
		}
		fputs("\n", fp);
	}
	fclose(fp);
	fp = fopen("Media.idt", "w");
	fputs(
		"DiskId\tLastSequence\tDiskPrompt\tCabinet\tVolumeLabel\tSource\n"
		"i2\ti2\tL64\tS255\tS32\tS72\n"
		"Media\tDiskId\n", fp);
	fprintf(fp, "1\t%u\t\t#%sarchive.cab\t\t\n", fileTable.len / fileCols,
		idPrefix);
	fclose(fp);
	if (renameFiles == true)
	{
		char* filename = "cablist.txt";
		char* pathname;
		pathname = (char*)xmalloc(strlen(rootDir.name) + 1 +
								  strlen(filename) + 1);
		sprintf(pathname, "%s/%s", rootDir.name, filename);
		fp = fopen(pathname, "w");
		for (i = 0; i < fileTable.len / fileCols; i++)
			fprintf(fp, "%sf%u\n", idPrefix, i);
		fclose(fp);
		xfree(pathname);
	}
}

int LSRAddBody(unsigned curLevel, char_array* colonLabel)
{
	unsigned curPos;
	char_array dirName;
	unsigned pathPart;
    /* `backDirs' is true if the code had to go up in the directory
	   hierarchy, as in `cd ..'.  "Back" is for backwards in a path
	   name.  */
	bool backDirs;

	curPos = 0;
	EA_INIT(char, dirName, 16);
	EA_APPEND(dirName, '\0');

	pathPart = 0;
	backDirs = false;

	/* Parse the colon label into its directory components.  */
	while (true)
	{
		if (colonLabel->d[curPos] == '/' || colonLabel->d[curPos] == '\0')
		{
			/* Check with the directory stack.  */
			if (dirStack.len > pathPart &&
				strcmp(dirStack.d[pathPart], dirName.d) != 0)
			{
				/* Pop all later directories off of the stack.  */
				unsigned i;
				for (i = dirStack.len - 1;
					 i >= pathPart && i != (unsigned)-1; i--)
				{
					xfree(dirStack.d[i]);
					EA_POP_BACK(dirStack);
					EA_NORMALIZE(dirStack);
					EA_POP_BACK(dirStkAssoc);
					EA_NORMALIZE(dirStkAssoc);
				}
				backDirs = true;
			}
			else if (dirStack.len <= pathPart)
				backDirs = false;
			if ((dirStack.len > pathPart &&
				 strcmp(dirStack.d[pathPart], dirName.d) != 0) ||
				dirStack.len <= pathPart)
			{
				char* newDirPart;
				DirTree* existDir;
				newDirPart = (char*)xmalloc(dirName.len);
				strcpy(newDirPart, dirName.d);
				EA_APPEND(dirStack, newDirPart);
				EA_APPEND(dirStkAssoc, dirTable.len);
				/* If the directory already exists, add the
				   existing index.  */
				if (firstList == false && dirStack.len >= 2)
				{
					char* pathName;
					unsigned pathLen;
					unsigned j;
					pathLen = 0;
					for (j = 0; j < dirStack.len; j++)
						pathLen += strlen(dirStack.d[j]) + 1;
					pathLen--; /* Don't include trailing slash */
					pathName = (char*)xmalloc(pathLen + 1);
					pathName[0] = '\0';
					for (j = 0; j < dirStack.len; j++)
					{
						strcat(pathName, dirStack.d[j]);
						if (j != dirStack.len - 1)
							strcat(pathName, "/");
					}
					existDir = FindAnyDirTree(pathName);
					xfree(pathName);
				}
				if (firstList == false && dirStack.len >= 2 && existDir)
				{
					/* Associate the directory added to the stack with
					   the existing index.  */
					dirStkAssoc.d[dirStkAssoc.len-1] =
						existDir->tableRow * dirCols;
				}
				else if (firstList == false && dirStkAssoc.len == 1)
				{
					/* Associate the index with the root.  */
					dirStkAssoc.d[dirStkAssoc.len-1] = 0;
				}
			}
			/* Clear the directory name.  */
			dirName.len = 1;
			dirName.d[0] = '\0';
			pathPart++;
			if (colonLabel->d[curPos] == '\0')
				break;
		}
		else
			EA_INSERT(dirName, dirName.len - 1, colonLabel->d[curPos]);
		curPos++;
	}
	xfree(dirName.d);

	/* Build the directory and component tables.  */
	/* Directories only count as components if there are files other
	   than directories in it.  */
	{
		unsigned colStart;
		unsigned dirTableRow;
		char* newDir;
		DirTree* existDir;
		/* If this isn't the first time, never add a new
		   directory for the root.  */
		if (firstList == false)
		{
			char* pathName;
			unsigned pathLen;
			unsigned j;
			pathLen = 0;
			for (j = 0; j < dirStack.len; j++)
				pathLen += strlen(dirStack.d[j]) + 1;
			pathLen--; /* Don't include trailing slash */
			pathName = (char*)xmalloc(pathLen + 1);
			pathName[0] = '\0';
			for (j = 0; j < dirStack.len; j++)
			{
				strcat(pathName, dirStack.d[j]);
				if (j != dirStack.len - 1)
					strcat(pathName, "/");
			}
			if (dirStack.len > 1)
				existDir = FindAnyDirTree(pathName);
			xfree(pathName);
		}
		if (firstList == true || (dirStack.len > 1 && existDir == NULL))
		{
			/* Add a directory row.  */
			colStart = dirTable.len;
			EA_SET_SIZE(dirTable, dirTable.len + dirCols);
			dirID = (char*)xmalloc(strlen(idPrefix) + 1 + 11 + 1);
			dirTableRow = dirTable.len / dirCols - 1;
			sprintf(dirID, "%sd%u", idPrefix, dirTable.len / dirCols - 1);
			dirTable.d[colStart] = dirID;
			newDir = (char*)xmalloc(strlen(dirID) + 1 +
									strlen(dirStack.d[dirStack.len-1]) + 1);
			newDir[0] = '\0';
			strcat(newDir, dirID);
			strcat(newDir, "|");
			strcat(newDir, dirStack.d[dirStack.len-1]);
			dirTable.d[colStart+2] = newDir;
			/* Connect the parent directory.  */
			if (dirStkAssoc.len > 1)
			{
				dirTable.d[colStart+1] =
					dirTable.d[dirStkAssoc.d[dirStkAssoc.len-2]];
			}
			else
				dirTable.d[colStart+1] = progDirID;
		}
		else if (firstList == false && dirStack.len > 1)
		{
			colStart = existDir->tableRow * dirCols;
			dirID = existDir->dirKey;
			dirTableRow = existDir->tableRow;
		}

		/* Add a directory tree item.  */
		if ((firstList == true && backDirs == false && pathPart == 1) ||
			(firstList == false && dirStack.len <= 1))
		{
			if (firstList == true)
			{
				/* This is the first time visiting the first root
				   directory (curDir == &rootDir).  */
				curDir->name = strchr(dirTable.d[colStart+2], (int)'|') + 1;
			}
			else
			{
				char* rootName;
				dirID = rootDir.dirKey;
				/* Initialize another root directory.  */
				colStart = 0;
				rootName = (char*)xmalloc(strlen(dirStack.d[0]) + 1);
				strcpy(rootName, dirStack.d[0]);
				curDir->name = rootName;
				EA_APPEND(rootNameN, rootName);
			}
			curDir->tableRow = 0;
			curDir->dirKey = dirTable.d[colStart];
			curDir->component = NULL;
			curDir->compRefCount = 0;
			curDir->fileComps = false;
			EA_INIT(DirTree, curDir->children, 16);
			EA_INIT(unsigned, curDir->fileIdcs, 16);
		}
		else
		{
			char* longName;
			if (backDirs == true)
			{
				unsigned i;
				/* Check which root we will use.  */
				if (firstList == true)
					curDir = &rootDir;
				else
					curDir = &rootDirN.d[curRoot];
				/* Traverse the directory hierarchy.  */
				for (i = 0; i < dirStack.len - 1; i++)
				{
					unsigned j;
					for (j = 0; j < curDir->children.len; j++)
					{
						if (strcmp(dirStack.d[i],
								   curDir->children.d[j].name) == 0)
						{
							curDir = &curDir->children.d[j];
							break;
						}
					}
				}
			}
			/* Initialize and add the directory tree item.  */
			longName = strchr(dirTable.d[colStart+2], (int)'|') + 1;
			curDir->children.d[curDir->children.len].name = longName;
			curDir->children.d[curDir->children.len].tableRow = dirTableRow;
			curDir->children.d[curDir->children.len].dirKey =
				dirTable.d[colStart];
			curDir->children.d[curDir->children.len].component = NULL;
			curDir->children.d[curDir->children.len].compRefCount = 0;
			curDir->children.d[curDir->children.len].fileComps = false;
			EA_INIT(DirTree, (curDir->children.d[curDir->
											 children.len].children), 16);
			EA_INIT(unsigned, (curDir->children.d[curDir->
											  children.len].fileIdcs), 16);
			EA_ADD(curDir->children);
			curDir = &curDir->children.d[curDir->children.len-1];
		}
	}

	/* Set parser state variables.  */
	addedComponent = false;
	return 1;
}

int LSRRemoveLevels(unsigned testLevel)
{
	/* This callback is not needed.  */
	return 1;
}

int LSRAddItem(char_array* itemName)
{
	if (addedComponent == false)
	{
		unsigned colStart;
		char* compID;
		colStart = compTable.len;
		EA_SET_SIZE(compTable, compTable.len + compCols);
		compID = (char*)xmalloc(strlen(idPrefix) + 1 + 11 + 1);
		sprintf(compID, "%sc%u", idPrefix, compTable.len / compCols - 1);
		compTable.d[colStart] = compID;
		compTable.d[colStart+1] = GetUuid();
		compTable.d[colStart+2] = dirID;
		compTable.d[colStart+3] = "2";
		compTable.d[colStart+4] = ""; /* Condition */
		/* We will set the last column after we parse the file name.  */

		/* Connect the component to its directory.  */
		curDir->component = compID;
	}

	/* Add a file table entry.  */
	{
		unsigned colStart;
		char* fileID;
		char* newFile;
		char* fileSize;
		char* seqNum;
		colStart = fileTable.len;
		EA_SET_SIZE(fileTable, fileTable.len + fileCols);
		fileID = (char*)xmalloc(strlen(idPrefix) + 1 + 11 + 1);
		sprintf(fileID, "%sf%u", idPrefix, fileTable.len / fileCols - 1);
		newFile = (char*)xmalloc(strlen(fileID) + 1 + itemName->len);
		newFile[0] = '\0';
		strcat(newFile, fileID);
		strcat(newFile, "|");
		strcat(newFile, itemName->d);
		fileTable.d[colStart] = fileID;
		fileTable.d[colStart+1] = compTable.d[compTable.len-6];
		fileTable.d[colStart+2] = newFile;
		/* Get the file size.  */
		{
			unsigned sizeNum;
			char* filePath;
			unsigned pathLen;
			char* newPathName;
			unsigned j;
			FILE* sizeFP;
			pathLen = 0;
			for (j = 0; j < dirStack.len; j++)
				pathLen += strlen(dirStack.d[j]) + 1;
			filePath = (char*)xmalloc(pathLen + itemName->len);
			newPathName = (char*)xmalloc(strlen(rootDir.name) + 1 +
										 strlen(fileID) + 1);
			filePath[0] = '\0';
			newPathName[0] = '\0';
			for (j = 0; j < dirStack.len; j++)
			{
				strcat(filePath, dirStack.d[j]);
				strcat(filePath, "/");
			}
			strcpy(newPathName, rootDir.name);
			strcat(newPathName, "/");
			strcat(filePath, itemName->d);
			strcat(newPathName, fileID);
			sizeFP = fopen(filePath, "rb");
			if (sizeFP == NULL)
			{
				fprintf(stderr, "ERROR: Could not open file: %s\n",
						filePath);
				xfree(filePath);
				xfree(newPathName);
				return 0;
			}
			fseek(sizeFP, 0, SEEK_END);
			sizeNum = ftell(sizeFP);
			fclose(sizeFP);
			if (renameFiles == true)
				rename(filePath, newPathName);
			xfree(filePath);
			xfree(newPathName);
			fileSize = (char*)xmalloc(11 + 1);
			sprintf(fileSize, "%u", sizeNum);
		}
		fileTable.d[colStart+3] = fileSize;
		fileTable.d[colStart+4] = ""; /* Version */
		fileTable.d[colStart+5] = ""; /* Language */
		fileTable.d[colStart+6] = "0";
		seqNum = (char*)xmalloc(11 + 1);
		sprintf(seqNum, "%u", fileTable.len / fileCols);
		fileTable.d[colStart+7] = seqNum;
		/* Add an index to the fileTable row in curDir.  */
		EA_APPEND(curDir->fileIdcs, colStart);
		/* Update component information.  */
		curDir->compRefCount++;
		if (addedComponent == false)
		{
			unsigned colStart;
			colStart = compTable.len - compCols;
			compTable.d[colStart+5] = fileID;
			addedComponent = true;
		}
	}
	return 1;
}

int FeatAddBody(unsigned curLevel, char_array* colonLabel)
{
	unsigned colStart;
	char* featureID;
	char* dispOrder;
	char* localFeatLabel;

	/* Add a feature stack entry.  */
	featStack.d[featStack.len] = (char*)xmalloc(colonLabel->len);
	strcpy(featStack.d[featStack.len], colonLabel->d);
	EA_ADD(featStack);
	featStkAssoc.d[featStkAssoc.len] = featureTable.len / featureCols;
	EA_ADD(featStkAssoc);

	/* Add a Feature entry.  */
	colStart = featureTable.len;
	EA_SET_SIZE(featureTable, featureTable.len + featureCols);
	featureID = (char*)xmalloc(strlen(idPrefix) + 2 + 11 + 1);
	sprintf(featureID, "%sft%u", idPrefix, featureTable.len / featureCols - 1);
	dispOrder = (char*)xmalloc(11 + 1);
	sprintf(dispOrder, "%u", (featureTable.len / featureCols) * 2);
	localFeatLabel = (char*)xmalloc(colonLabel->len);
	strcpy(localFeatLabel, colonLabel->d);
	featureTable.d[colStart] = featureID;
	if (featStack.len > 1)
	{
		unsigned index;
		index = featStkAssoc.d[featStkAssoc.len-2];
		featureTable.d[colStart+1] = featureTable.d[index*featureCols];
	}
	else
		featureTable.d[colStart+1] = "";
	featureTable.d[colStart+2] = localFeatLabel;
	featureTable.d[colStart+3] = featureTable.d[colStart+2];
	featureTable.d[colStart+4] = dispOrder;
	featureTable.d[colStart+5] = "3";
	featureTable.d[colStart+6] = progDirID;
	if (featStack.len == 1)
		featureTable.d[colStart+7] = "0";
	else
		featureTable.d[colStart+7] = "2";

	/* Set parser state variables.  */
	reusedComponent = false;
	return 1;
}

int FeatRemoveLevels(unsigned testLevel)
{
	/* Pop features off of the feature stack.  */
	unsigned i;
	for (i = featStack.len; i > testLevel; i--)
	{
		xfree(featStack.d[i-1]);
		EA_POP_BACK(featStack);
		EA_POP_BACK(featStkAssoc);
	}
	return 0;
}

int FeatAddItem(char_array* itemName)
{
	unsigned i;
	char_array pathPart;
	bool foundDir;
	bool skippedRoot;
	EA_INIT(char, pathPart, 16);
	EA_APPEND(pathPart, '\0');
	/* Parse the path until the end file.  */
	curDir = &rootDir;
	skippedRoot = false;
	for (i = 0; i < itemName->len; i++) /* Include the null character */
	{
		if (itemName->d[i] == '/' || itemName->d[i] == '\0')
		{
			unsigned j;
			foundDir = false;
			if (skippedRoot == false)
			{
				/* Check which root we will use.  */
				if (strcmp(rootDir.name, pathPart.d) == 0)
					curDir = &rootDir;
				else
				{
					unsigned i;
					for (i = 0; i < rootDirN.len; i++)
					{
						if (strcmp(rootDirN.d[i].name, pathPart.d) == 0)
						{
							curDir = &rootDirN.d[i];
							break;
						}
					}
					if (i == rootDirN.len)
					{
						fprintf(stderr, "ERROR: Invalid root directory "
								"in \"features.txt\": %s.\n", pathPart.d);
						xfree(pathPart.d);
						return 0;
					}
				}
				skippedRoot = true;
				if (itemName->d[i] != '\0')
				{
					pathPart.d[0] = '\0';
					EA_SET_SIZE(pathPart, 1);
				}
				continue;
			}
			for (j = 0; j < curDir->children.len; j++)
			{
				if (strcmp(curDir->children.d[j].name, pathPart.d) == 0)
				{
					curDir = &curDir->children.d[j];
					pathPart.d[0] = '\0';
					EA_SET_SIZE(pathPart, 1);
					foundDir = true;
					break;
				}
			}
			if (foundDir == false)
			{
				/* This might be a file and not a directory.  */
				break;
			}
		}
		else
			EA_INSERT(pathPart, pathPart.len - 1, itemName->d[i]);
	}
	if (curDir == &rootDir && strcmp(itemName->d, pathPart.d) == 0 &&
		strcmp(rootDir.name, itemName->d) != 0)
	{
		fprintf(stderr, "ERROR: Invalid directory specified "
				"within \"features.txt\": %s.\n", itemName->d);
		return 0;
	}
	else if (foundDir == false && strcmp(itemName->d, pathPart.d) != 0 &&
			 pathPart.len > 1)
	{
		/* Add an individual file.  */
		unsigned colStart = (unsigned)-1;
		bool addedComponent = false;
		char* compID;
		unsigned i;

		/* Find the table index of the current file.  */
		for (i = 0; i < curDir->fileIdcs.len; i++)
		{
			unsigned testIndex;
			testIndex = curDir->fileIdcs.d[i];
			if (strcmp(strchr(fileTable.d[testIndex+2], (int)'|') + 1,
					   pathPart.d) == 0)
			{
				colStart = testIndex;
				break;
			}
		}
		if (colStart == (unsigned)-1)
		{
			fprintf(stderr, "ERROR: Invalid file name specified "
					"within \"features.txt\": %s.\n", itemName->d);
			return 0;
		}

		if ((reusedComponent == false || curDir != lastDir) &&
			curDir->fileComps == true)
		{
			/* Create a new component.  */
			unsigned compColStart;
			compColStart = compTable.len;
			EA_SET_SIZE(compTable, compTable.len + compCols);
			compID = (char*)xmalloc(strlen(idPrefix) + 1 + 11 + 1);
			sprintf(compID, "%sc%u", idPrefix, compTable.len / compCols - 1);
			compTable.d[compColStart] = compID;
			compTable.d[compColStart+1] = GetUuid();
			compTable.d[compColStart+2] = curDir->dirKey;
			compTable.d[compColStart+3] = "2";
			compTable.d[compColStart+4] = ""; /* Condition */
			compTable.d[compColStart+5] = fileTable.d[colStart];
			addedComponent = true;
		}
		else
		{
			compID = curDir->component;
			curDir->fileComps = true;
		}
		if (reusedComponent == false || addedComponent == true)
		{
			/* Associate the component with the given feature.  */
			unsigned colStart;
			unsigned featColStart;
			colStart = featCompTable.len;
			featColStart = featureTable.len - featureCols;
			EA_SET_SIZE(featCompTable, featCompTable.len + featCompCols);
			featCompTable.d[colStart] = featureTable.d[featColStart];
			featCompTable.d[colStart+1] = compID;
			if (curDir->component != compID)
				curDir->compRefCount--;
			lastDir = curDir;
			reusedComponent = true;
		}
		/* Associate the file with the component.  */
		fileTable.d[colStart+1] = compID;
	}
	else
	{
		/* Recursively add a directory.  */
		unsigned colStart;
		char* featureID;
		colStart = featureTable.len - featureCols;
		featureID = featureTable.d[colStart];
		AddFeatComps(&featCompTable, featureID, curDir);
	}
	xfree(pathPart.d);
	return 1;
}

char* GetUuid()
{
	char* uuid;
	uuid = (char*)xmalloc(38 + 2 + 1);
	fgets(uuid + 1, 41, uuidFP);
	uuid[0] = '{';
	uuid[37] = '}';
	uuid[38] = '\0';
	return uuid;
}

/* This function takes a filename and returns the index in the file
   table for which that file's information is located.  In this
   function, `end' refers to the element one position beyond the end
   of the list.  */
/* This function is untested.  */
unsigned FindFile(FileIndex_array* database, char* filename,
				  unsigned begin, unsigned end)
{
	unsigned middle;
	if (end - begin == 0)
		return (unsigned)-1;
	middle = (begin + end) / 2;
	if (strcmp(database->d[middle*fileCols+2].name, filename) == 0)
		return database->d[middle*fileCols+2].tableIndex;
	else if (strcmp(database->d[middle*fileCols+2].name, filename) < 0)
		return FindFile(database, filename, begin, middle);
	else
		return FindFile(database, filename, middle + 1, end);
}

/* Parse a path and search all root directory collections until there
   is a path match.  The root prefix of the path is ignored during the
   search.  */
DirTree* FindAnyDirTree(char* path)
{
	char* pathToFind;
	DirTree* tree;
	unsigned i;
	pathToFind = strchr(path, (int)'/') + 1;
	tree = FindDirTree(&rootDir, pathToFind);
	if (tree != NULL)
		return tree;
	for (i = 0; i < rootDirN.len; i++)
	{
		tree = FindDirTree(&rootDirN.d[i], pathToFind);
		if (tree != NULL)
			return tree;
	}
	return NULL;
}

/* Given a root directory tree, parses a path and traverses the
   directory tree until the corresponding DirTree structure is found.
   "path" must not include the root prefix.  */
DirTree* FindDirTree(DirTree* rootDir, char* path)
{
	char* substr;
	char* slashPos;
	unsigned subLen;
	unsigned i;
	bool lastPart;
	slashPos = strchr(path, (int)'/');
	if (slashPos == NULL)
	{
		/* This is the last part to look for.  */
		lastPart = true;
		substr = path;
	}
	else
	{
		lastPart = false;
		subLen = slashPos - path;
		substr = (char*)xmalloc(subLen + 1);
		strncpy(substr, path, subLen);
		substr[subLen] = '\0';
	}
	for (i = 0; i < rootDir->children.len; i++)
	{
		if (strcmp(rootDir->children.d[i].name, substr) == 0)
		{
			if (lastPart == false)
			{
				xfree(substr);
				return FindDirTree(&rootDir->children.d[i], slashPos + 1);
			}
			else
				return &rootDir->children.d[i];
		}
	}
	if (lastPart == false)
		xfree(substr);
	return NULL;
}

/* Recursively associate components for a feature given a DirTree
   structure to traverse.  */
void AddFeatComps(char_ptr_array* featCompTable, char* featureID,
				  DirTree* curDir)
{
	unsigned colStart;
	unsigned i;

	if (curDir->component != NULL)
	{
		colStart = featCompTable->len;
		EA_SET_SIZE(*featCompTable, featCompTable->len + featCompCols);
		featCompTable->d[colStart] = featureID;
		featCompTable->d[colStart+1] = curDir->component;
	}
	for (i = 0; i < curDir->children.len; i++)
		AddFeatComps(featCompTable, featureID, &curDir->children.d[i]);
}

void FreeDirTree(DirTree* dir)
{
	unsigned i;
	for (i = 0; i < dir->children.len; i++)
		FreeDirTree(&dir->children.d[i]);
	xfree(dir->children.d);
	xfree(dir->fileIdcs.d);
}
