#pragma once
#include "platform.h"
void xtgvPresenterOpen(int width, int height, const char *title);
void xtgvPresenterPublish(XtgvDesktopCanvas &canvas);
void xtgvPresenterPoll();
void xtgvPresenterClose();
