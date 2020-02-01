/* This is basically just boolean definitions.  */
/*

Public Domain 2013, 2020 Andrew Makousky

See the file "UNLICENSE" in the top level directory for details.

*/
#ifndef BOOL_H
#define BOOL_H

#ifndef __cplusplus
/* C Language extensions.  */
enum bool_tag { false, true };
typedef enum bool_tag bool;
#endif

#endif /* not BOOL_H */
