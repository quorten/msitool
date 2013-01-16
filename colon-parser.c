/* colon-parser.c -- parse an `ls -R' like file, that may have tab
   indentations.

   To use this parser, call ParseLSRFile() with the appropriate
   callbacks.  */

#include <stdio.h>
#include <string.h>

#include "bool.h"
#include "xmalloc.h"
#define ea_malloc xmalloc
#define ea_realloc xrealloc
#define ea_free xfree
#include "exparray.h"

/* Define necessary types before including local headers.  */
EA_TYPE(char);

/* Local includes */
#include "colon-parser.h"

/* Parse a ls -R like file, that may have tab indentations.  Each
   header that has a colon following it is called a label.
   `ParseLSRFile()' has support for files that aren't true `ls -R'
   files that have indentation to show the nesting level rather than
   only using a path name to indicate nesting.  Returns nonzero on
   success, zero on failure.  See `colon-parser.h' for documentation
   on the callback functions.  */
int ParseLSRFile(FILE* fp, AddBodyClbk AddBody,
				 RemoveLevelsClbk RemoveLevels, AddItemClbk AddItem)
{
	int readChar;
	unsigned curLevel; /* current nesting level */
	unsigned testLevel; /* test a new nesting level */
	bool subLevel;
	char_array colonLabel;

	readChar = fgetc(fp);
	curLevel = 0;
	testLevel = 0;
	subLevel = false;
	EA_INIT(char, colonLabel, 16);

	while (readChar != EOF)
	{
		/* We will assume that there is no extra information at the
		   top of the file.  */

		if (subLevel == false)
		{
			/* Read the label that is followed by a colon.  */
			EA_APPEND(char, colonLabel, '\0');
			while (readChar != EOF && (char)readChar != ':')
			{
				unsigned pos = colonLabel.len - 1;
				EA_INSERT(char, colonLabel, pos, (char)readChar);
				readChar = fgetc(fp);
			}
			if (readChar == EOF)
				break;
			curLevel++;
		}
		else
		{
			/* Use the previous label that was saved.  */
			subLevel = false;
			curLevel++;
		}

		/* Data processing hook */
		if (!AddBody(curLevel, &colonLabel))
			goto failure;

		readChar = fgetc(fp); /* Read the newline.  */
		if ((char)readChar == '\r')
		{
			fputs("ERROR: Found a non-Unix newline character in "
				  "the input stream.\n", stderr);
			goto failure;
		}

		/* Fill the body.  */
		/* Read until the double newline or a label.  */
		readChar = fgetc(fp);
		while (readChar != EOF)
		{
			char_array itemName;
			int result;

			if ((char)readChar == '\n') /* double newline */
				break;

			/* Read any indents.  */
			testLevel = 0;
			while ((char)readChar != EOF && (char)readChar == '\t')
			{
				readChar = fgetc(fp);
				testLevel++;
			}
			if (testLevel < curLevel)
			{
				/* The indentation level has decreased.  */
				/* Data processing hook */
				result = RemoveLevels(testLevel);
				curLevel = testLevel;
				if (result == 0)
					break;
				if (result == 2)
					goto failure;
			}

			EA_INIT(char, itemName, 16);
			EA_APPEND(char, itemName, '\0');
			while (readChar != EOF && (char)readChar != '\n' &&
				(char)readChar != ':')
			{
				unsigned pos = itemName.len - 1;
				EA_INSERT(char, itemName, pos, (char)readChar);
				readChar = fgetc(fp);
			}
			if (readChar == EOF)
			{
				xfree(itemName.d);
				break;
			}
			if (readChar == ':')
			{
				/* A nested label was found.  */
				/* Prepare for next loop.  */
				EA_SET_SIZE(char, colonLabel, itemName.len);
				strcpy(colonLabel.d, itemName.d);
				subLevel = true;
				xfree(itemName.d);
				break;
			}

			/* Data processing hook */
			result = AddItem(&itemName);

			xfree(itemName.d);
			if (!result)
				goto failure;
			readChar = fgetc(fp);
		}

		if (subLevel == true)
			continue;
		EA_SET_SIZE(char, colonLabel, 0);
		if (readChar == EOF)
			break;
		if ((char)readChar == '\n')
			readChar = fgetc(fp);
	}

	xfree(colonLabel.d);
	return 1;

failure:
	xfree(colonLabel.d);
	return 0;
}
