## This Fork

I forked sxhkd (simple X hotkey daemon) into sxhkd (SLOPCODE X hotkey daemon),
a fork which adds a visual indicator for key chains. Think of it like Vim's
mode indicator, that improved on Vi's raw mode modeing. This extension is now
what it could have been from the start: a small separate binary,
`sxhkd-indicator`, reading sxhkd's existing FIFO interface. That interface was
so poorly documented that I only noticed it as a real option after already
having forked the original project, so the indicator first lived inside the
daemon; it has since moved out, and the daemon itself links nothing but xcb.


## Description

*sxhkd* is an X daemon that reacts to input events by executing commands.

Its configuration file is a series of bindings that define the associations
between the input events and the commands.

The format of the configuration file supports a simple notation for mapping
multiple shortcuts to multiple commands in parallel.

Chord chains (`super + m ; h`) and locked chains (`super + n : {h,j,k,l}`) act
like modes. `sxhkd-indicator` shows a small on-screen indicator listing the
chords received so far while a chain is in progress, so that you always know
which mode you are in. It reads the daemon's status FIFO, so the two are
started together:

	sxhkd -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &
	sxhkd-indicator -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &

Every option has a short and a long form; `sxhkd --help` and
`sxhkd-indicator --help` list them all.


### sxhkd-indicator

The indicator is a program of its own: it learns what the daemon is doing from
the status FIFO and never touches the daemon, so it can be started, stopped,
restyled or replaced at will. Either side may start first: whichever finds no
FIFO at the path creates it. The indicator waits for the daemon before opening
the display, hides the banner when the daemon exits and waits for the next one,
so it survives `sxhkd` restarts. `-i` anchors the banner (`top-right` by
default), `-f` sets its font (a Pango description), `-F` and `-B` its text and
background colors as `#rrggbb` or `#rrggbbaa`; translucency needs a compositing
manager and a 32-bit visual (without the latter the colors are painted opaque).
`-t` hides a chain shown in progress after that many seconds without a status
line (default 3, 0 never), in case the line that ended it was lost.

	sxhkd -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &
	sxhkd-indicator -s "$XDG_RUNTIME_DIR/sxhkd.fifo" -i bottom -f "monospace 14" -F '#ffffff' -B '#222222c0' &

A notification script (see the next section) reads a pipe of its own, through
a second `-s` on the daemon, so that neither consumer steals the other's lines:

	sxhkd -s "$XDG_RUNTIME_DIR/sxhkd.fifo" -s "$XDG_RUNTIME_DIR/sxhkd-notify.fifo" &
	sxhkd-indicator -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &
	examples/notification/sxhkd_notify "$XDG_RUNTIME_DIR/sxhkd-notify.fifo" &

`sxhkd-indicator --help` and `sxhkd-indicator(1)` have the details.


## Status FIFO

With the `-s` option, *sxhkd* reports what it is doing to a named pipe, so that
a notification script or a status bar can show the chord chain in progress or
react to the commands being run. *sxhkd* creates the pipe (owner-only) if it
does not exist and removes it at exit in that case; an existing pipe is used and
left alone, anything else at the path is a startup error. `-s` may be given
several times (at most 8): every message goes to each pipe, so an indicator, a
notification script and a status bar can each read their own.

	sxhkd -s "$XDG_RUNTIME_DIR/sxhkd.fifo" &

Each message is one line: a one-character prefix, then a text.

| Prefix | When                         | Text                                        |
|--------|------------------------------|---------------------------------------------|
| `H`    | a chord was received         | the chords received so far, `;`-separated   |
| `B`    | a chord chain has begun      | `Begin chain`                               |
| `L`    | the chord chain was locked   | `Chain locked` (after the `H` of the `:` chord) |
| `A`    | the abort keysym ended it    | `Chain aborted` (followed by an `E` line)   |
| `E`    | the chord chain has ended    | `End chain`                                 |
| `T`    | the chord chain timed out    | `Timeout reached` (followed by an `E` line) |
| `C`    | a command has been started   | the command                                 |

This is version 2 of the protocol (`L` and `A` are new): a consumer must ignore
the prefixes it does not know, and the existing prefixes keep their meaning and
their relative order in later versions.

Pressing `super + m` then `h` for the binding `super + m ; h` produces:

	Hsuper + m
	BBegin chain
	Hsuper + m;h
	EEnd chain
	Cecho H

For the locked binding `super + m : h`, pressing `super + m`, `h`, then
`Escape` produces:

	Hsuper + m
	BBegin chain
	LChain locked
	Hsuper + m;h
	Cecho H
	AChain aborted
	EEnd chain

A single-chord binding only produces its `H` and `C` lines. A consumer waits
for the pipe, then reads it line by line and strips the prefix:

	until [ -p "$XDG_RUNTIME_DIR/sxhkd.fifo" ]; do sleep 0.1; done
	while read -r line; do
	    case $line in
	        H*) notify-send -t 2000 "sxhkd" "${line#?}" ;;
	        C*) notify-send -t 4000 "sxhkd" "Running: ${line#?}" ;;
	        L*) notify-send -t 2000 "sxhkd" "Chain locked" ;;
	        A*) notify-send -t 1000 "sxhkd" "Chain aborted" ;;
	        T*) notify-send -t 1000 "sxhkd" "Chain timed out" ;;
	    esac
	done < "$XDG_RUNTIME_DIR/sxhkd.fifo"

See `examples/notification` for a complete setup, and the man page for the
details of when each message is sent.


## Building

`make` builds the two programs; `make PROGRAMS=sxhkd` (or
`PROGRAMS=sxhkd-indicator`) builds, installs or uninstalls just one of them.
Each has its own dependencies:

- `sxhkd`: libxcb, xcb-util-keysyms, xcb-util (`xcb_event.h`); nothing else,
  and no `pkg-config`.
- `sxhkd-indicator`: the above, plus cairo (with its xcb backend), pango and
  pangocairo, located through `pkg-config`.


### Arch Linux

`contrib/arch/PKGBUILD` builds the checkout it lives in as two packages:
`sxhkd-git`, which replaces the official `sxhkd`, and `sxhkd-indicator-git`,
which depends on it:

	cd contrib/arch && makepkg -si

Set `SXHKD_GIT_URL` to a clone URL to build another repository instead.


### Nix

`nix build` produces the daemon (`.#sxhkd`, the default output, whose closure
has neither cairo nor pango) and `nix build .#sxhkd-indicator` the indicator;
`nix flake check` builds both and runs the static analysis, and `nix develop`
opens a shell with every build and test tool.


### Tests

`make check` builds and runs the headless unit tests with ASan and UBSan
(`TEST_CFLAGS` overrides the sanitizers, e.g. `TEST_CFLAGS=` for none).
`-Werror` is opt-in, `make check WERROR=-Werror`, so that a warning added by a
newer compiler does not break package builds; `make analyze` always uses it.


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
