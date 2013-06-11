/* colon-parser.h -- parse a ls -R like file, that may have tab
   indentations.

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

#ifndef COLON_PARSER_H
#define COLON_PARSER_H

/* Callback functions */

/* Add Body Callback

   This callback is called by `ParseLSRFile()' whenever it encounters
   a header construct followed by a colon.  When parsing a file that
   has tabs at the left margin, `ParseLSRFile()' will give this
   callback a nesting level.  The nesting level starts at one and is
   increased for each tab that occurs at the left margin.

   Parameters:
   unsigned curLevel -- the current nesting level.
   char_array* colonLabel -- a null-terminated string specifying the
       text of the header before the colon and after any leading tabs

   Return value: Nonzero on success, zero on failure */
typedef int (* AddBodyClbk)(unsigned, char_array*);

/* Remove Levels Callback

   This callback is called by `ParseLSRFile()' whenever the
   indentation level decreases.

   Parameters:
   unsigned testLevel -- the new nesting level

   Return value: Zero to indicate the callback was processed, 1 to
   indicate the callback was ignored, and 2 to indicate the callback
   failed.  */
typedef int (* RemoveLevelsClbk)(unsigned);

/* Add Item Callback

   This callback is called by `ParseLSRFile()' whenever it encounters
   an item (or file name) within the file being parsed.

   Parameters:
   char_array* itemName -- the name of the item as a null terminated
                           string

   Return value: Nonzero on success, zero on failure */
typedef int (* AddItemClbk)(char_array*);

int ParseLSRFile(FILE* fp, AddBodyClbk AddBody,
				 RemoveLevelsClbk RemoveLevels, AddItemClbk AddItem);

#endif /* COLON_PARSER_H */
