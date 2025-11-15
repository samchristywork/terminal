![Banner](https://s-christy.com/sbs/status-banner.svg?icon=action/terminal&hue=200&title=Terminal&description=A%20lightweight%20terminal%20emulator%20built%20with%20X11%20and%20Xft)

## Overview

This is a lightweight terminal emulator written in C using the X11 and Xft
libraries for rendering. It implements a substantial subset of the VT100/VT220
and ANSI escape code standards, supporting features such as alternate screen
buffers, mouse reporting, bracketed paste, and a scrollback buffer.

The emulator forks a child shell process and communicates with it through a
pseudoterminal (PTY), relaying input and output between the shell and the
graphical window. Double buffering via an X11 Pixmap keeps rendering
artifact-free even during rapid screen updates.

## Screenshots

<p align="center">
  <img src="./assets/screenshot.png" />
</p>

## Features

## Usage

## Dependencies

## License

This work is licensed under the GNU General Public License version 3 (GPLv3).

[<img src="https://s-christy.com/status-banner-service/GPLv3_Logo.svg" width="150" />](https://www.gnu.org/licenses/gpl-3.0.en.html)
