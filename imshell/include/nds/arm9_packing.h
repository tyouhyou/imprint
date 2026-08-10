#ifndef ARM9_PACKING_H
#define ARM9_PACKING_H

#include <string>
#include <nds.h>

/* print log and the win/lost information */
void clearscreen(void);
void clearinfo(void);
void printinfo(std::string str);
/* print the chess color of the next turn */
void printnext(std::string str);
/* print the white/black chess number in the board */
void printscore(int blacknum, int whitenum);
/* initiate the frame buffer mode */
void initFrameBuffer(void);
/* set the sub LCD to console mode */
void initSubVideoConsole(void);
/* initiate the double buffer mode */
void initDBFrameBuffer(void);
/* swap the buffer */
void swapBuffers(void);
/* process the key event */
bool processkey(void);

void printlog(std::string str, ...);

#endif // ARM9_PACKING_H