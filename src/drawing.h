#include <assert.h>

void DrawTextLineBG(uint8 *dest);
void DrawMessage(void);
void FCEU_DrawRecordingStatusN(uint8* XBuf, int n);
void FCEU_DrawNumberRow(uint8 *XBuf, int *nstatus, int cur);
void DrawTextTrans(uint8 *dest, uint32 width, uint8 *textmsg, uint8 fgcolor);