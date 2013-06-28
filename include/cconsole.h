#pragma once

#define		lcc_escape(x)	"\033[01;" #x "m"
#define		lcc_GRAY		lcc_escape(30)
#define		lcc_RED			lcc_escape(31)
#define		lcc_GREEN		lcc_escape(32)
#define		lcc_YELLOW		lcc_escape(33)
#define		lcc_BLUE		lcc_escape(34)
#define		lcc_PURPLE		lcc_escape(35)
#define		lcc_CYAN		lcc_escape(36)
#define		lcc_WHITE		lcc_escape(37)
#define		lcc_NORMAL		"\033[00m"
