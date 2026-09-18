## This Fork

I forked sxhkd (simple X hotkey daemon) into sxhkd (SLOPCODE X hotkey daemon),
a fork which adds a visual indicator for key chains. Think of it like Vim's
mode indicator, that improved on Vi's raw mode modeing. This extension could
also have been a small separate binary reading sxhkd's existing FIFO interface,
but that was so poorly documented that I only noticed it as a real option after
already having forked the original project, so this is what you (we, mostly me)
get.


## Description

*sxhkd* is an X daemon that reacts to input events by executing commands.

Its configuration file is a series of bindings that define the associations
between the input events and the commands.

The format of the configuration file supports a simple notation for mapping
multiple shortcuts to multiple commands in parallel.

Chord chains (`super + m ; h`) and locked chains (`super + n : {h,j,k,l}`) act
like modes. With the `-i` option, *sxhkd* shows a small on-screen indicator
listing the chords received so far while a chain is in progress, so that you
always know which mode you are in:

	sxhkd --indicator top-right --indicator-font "monospace 14" -F '#ffffff' -B '#222222'

Every option has a short and a long form; `sxhkd --help` lists them all.
Colors take an optional alpha (`-B '#222222c0'`); translucency needs a compositing manager.


## Status FIFO

With the `-s` option, *sxhkd* reports what it is doing to a named pipe, so that
a notification script or a status bar can show the chord chain in progress or
react to the commands being run. *sxhkd* creates the pipe (owner-only) if it
does not exist and removes it at exit in that case; an existing pipe is used and
left alone, anything else at the path is a startup error:

	sxhkd -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &

Each message is one line: a one-character prefix, then a text.

| Prefix | When                         | Text                                        |
|--------|------------------------------|---------------------------------------------|
| `H`    | a chord was received         | the chords received so far, `;`-separated   |
| `B`    | a chord chain has begun      | `Begin chain`                               |
| `E`    | the chord chain has ended    | `End chain`                                 |
| `T`    | the chord chain timed out    | `Timeout reached` (followed by an `E` line) |
| `C`    | a command has been started   | the command                                 |

Pressing `super + m` then `h` for the binding `super + m ; h` produces:

	Hsuper + m
	BBegin chain
	Hsuper + m;h
	EEnd chain
	Cecho H

A single-chord binding only produces its `H` and `C` lines. A consumer waits
for the pipe, then reads it line by line and strips the prefix:

	until [ -p "$XDG_RUNTIME_DIR/sxhkd.fifo" ]; do sleep 0.1; done
	while read -r line; do
	    case $line in
	        H*) notify-send -t 2000 "sxhkd" "${line#?}" ;;
	        C*) notify-send -t 4000 "sxhkd" "Running: ${line#?}" ;;
	        T*) notify-send -t 1000 "sxhkd" "Chain timed out" ;;
	    esac
	done < "$XDG_RUNTIME_DIR/sxhkd.fifo"

See `examples/notification` for a complete setup, and the man page for the
details of when each message is sent.


## Dependencies

- libxcb, xcb-util-keysyms, xcb-util (`xcb_event.h`)
- cairo (with its xcb backend), pango and pangocairo, located through
  `pkg-config`


### Arch Linux

`contrib/arch/PKGBUILD` builds the checkout it lives in as the `sxhkd-git`
package, which replaces the official `sxhkd`:

	cd contrib/arch && makepkg -si

Set `SXHKD_GIT_URL` to a clone URL to build another repository instead.


### Nix

`nix build` produces the package, `nix flake check` also runs the static
analysis, and `nix develop` opens a shell with every build and test tool.


## Example Bindings

	XF86Audio{Prev,Next}
		mpc -q {prev,next}

	@XF86LaunchA
		scrot -s -e 'image_viewer $f'

	super + shift + equal
		sxiv -rt "$HOME/image"

	XF86LaunchB
		xdotool selectwindow | xsel -bi

	super + {h,j,k,l}
		bspc node -f {west,south,north,east}

	super + alt + {0-9}
		mpc -q seek {0-9}0%

	super + {alt,ctrl,alt + ctrl} + XF86Eject
		sudo systemctl {suspend,reboot,poweroff}

	super + {_,shift + }{h,j,k,l}
		bspc node -{f,s} {west,south,north,east}

	{_,shift + ,super + }XF86MonBrightness{Down,Up}
		bright {-1,-10,min,+1,+10,max}

	super + o ; {e,w,m}
		{gvim,firefox,thunderbird}

	super + alt + control + {h,j,k,l} ; {0-9}
		bspc node @{west,south,north,east} -r 0.{0-9}

	super + alt + p
		bspc config focus_follows_pointer {true,false}

	# Smart resize, will grow or shrink depending on location.
	# Will always grow for floating nodes.
	super + ctrl + alt + {Left,Down,Up,Right}
	  n=10; \
	  { d1=left;   d2=right;  dx=-$n; dy=0;   \
	  , d1=bottom; d2=top;    dx=0;   dy=$n;  \
	  , d1=top;    d2=bottom; dx=0;   dy=-$n; \
	  , d1=right;  d2=left;   dx=$n;  dy=0;   \
	  } \
	  bspc node --resize $d1 $dx $dy || bspc node --resize $d2 $dx $dy


## Editor Plugins

### Vim
- [vim-sxhkdrc](https://github.com/baskerville/vim-sxhkdrc).
- [sxhkd-vim](https://github.com/kovetskiy/sxhkd-vim).

### VS Code
- [sxhkdrc-syntax](https://github.com/mosbasik/sxhkdrc-syntax).

### Emacs
- [sxhkd-mode](https://github.com/xFA25E/sxhkd-mode)
- [sxhkdrc-mode](https://github.com/protesilaos/sxhkdrc-mode)

## License

This fork is distributed under the GNU General Public License, version 3 or (at your option) any later version; see `LICENSE`. The original sxhkd code by Bastien Dejean is licensed under the BSD 2-Clause license, reproduced in `LICENSE.BSD-2-Clause` and in the headers of his files.

----

For further information, check the `man` pages.
