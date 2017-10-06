/* simple test to experiment with valgrind capacities 
 *
 * Compile as  
 *    gcc -g -o fail_mem_test fail_mem_test.c
 * Execute as
 *    valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all ./fail_mem_test
 * Identify which errors are captured and which are not. Correct them.
 */

#include <stdlib.h>

int main ()
{
    int *a;
    int array[3]; 
    char *b;


   *a = 3;         /* ERROR, pointer used without initialization */
   array[3] = 4;   /* ERROR, out-of-bounds write in array, valid positions are
                        0, 1, 2 */

    b = malloc(2);
    *(b+2)='5';    /* ERROR, writes out of bounds of the malloc'ed area */

                   /* ERROR, does not free memory allocated by malloc */

    return(0);
}



