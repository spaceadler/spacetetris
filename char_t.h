#ifndef CHAR_T_H
#define CHAR_T_H

// plain char, for text and keys, under a name. MISRA Dir 4.6 and JPL rule 17
// want named types in place of the basic ones. this typedef is the one place
// in the code the basic type is spelled out (Dir 4.6 allows exactly that),
// and char_t is the name MISRA's own examples use.
typedef char char_t;

#endif // CHAR_T_H
