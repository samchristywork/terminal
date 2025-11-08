#!/bin/sh
printf '\033[31mRed Text\033[0m Normal Text\n'
printf '\033[1;34mBold Blue\033[0m \033[42mGreen Background\033[0m\n'
printf '\033[4;33mUnderlined Yellow\033[0m\n'
printf '\033[7mReverse Video\033[0m\n'
printf '\033[38;5;196m256-color Red\033[0m \033[48;5;46m256-color Green BG\033[0m\n'
printf '\nTab Test:\tCol1\tCol2\tCol3\n'
printf 'Line with \033[31mred\033[0m and \033[34mblue\033[0m words.\n'
