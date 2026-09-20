## This Fork

`sxhkd`'s chains, manily the locked ones, kind-of behave like keybinding
submodes, very reminiscent of vi/vim's modes. This app was forked off from
upstream to implement a tool to show in-screen what "mode" we're currently on.
That was needed after repeatedly running hotkeys I didn't want while trying to
type text.

Originally, the mode indicator had been built into `sxhkd` itself, but it grew
kind-of too large and, because of that, I split it into a separate binary:
`sxhkd-indicator`. That requried adding functionality to the FIFO interface
though. This fork also adds better docs for that interface. 

Other features are `--long-option-names`, lots of bug fixes, more correctness
when handling signals, smaller UB/bug fixes, an Arch Linux PKGBUILD script,
better Makefile support and a nix flake around everything, for reproducible dev
environments and easier/more widely available packaging.


## `sxhkd`

*sxhkd* is an X daemon that reacts to input events by executing commands.

Its configuration file is a series of bindings that define the associations
between the input events and the commands.

The format of the configuration file supports a simple notation for mapping
multiple shortcuts to multiple commands in parallel. It also supports keychord
chains and locked keychord chains.

See `sxhkd --options` and `man 1 sxhkd` for more information.


## Status FIFO

With the `-s`/`--status-fifo` options, *sxhkd* reports what it is doing to a
named pipe, so that notification script, status bars or `sxhkd-indicator` can
show the chord chain in progress or react to the commands being run.

`sxhkd` creates the pipe (owner-only) if it does not exist and removes it at
exit in that case; an existing pipe is used and left alone. `-s` may be given
up to 8 times, so that the daemon can support notifying multiple consumers.

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

This is version 2 of the protocol, an innovation of this branch. Messages `L`
and `A` are new. For maximum compatibility, consumers should ignore the
prefixes they do not know.

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

See `examples/notification` for a complete setup, and the `man` page for the
details of when each message is sent.


## sxhkd-indicator

`sxhkd-indicator` shows a small on-screen indicator listing the chords received
so far while a chain is in progress, so that you always know which keybinding
"mode" you are in. It reads the daemon's status FIFO, so the two are started
together:

	sxhkd --status-fifo "$XDG_RUNTIME_DIR/sxhkd.fifo" &
	sxhkd-indicator --status-fifo "$XDG_RUNTIME_DIR/sxhkd.fifo" &

Either side may start first: whichever finds no FIFO at the path creates it.
The indicator waits for the daemon before opening the display, hides the banner
when the daemon exits and waits for the next one, so it survives `sxhkd`
restarts. The look, position and font used by the indicator may be configured
with CLI flags. Run `sxhkd-indicator --help` for more information.

This indicator supports pango descriptions for fonts and translucency for its
colors -- though that requires running a compositor alongside this app.

`sxhkd-indicator --help` and `sxhkd-indicator(1)` have the details.


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

This fork is distributed under the GNU General Public License, version 3 or (at
your option) any later version; see `LICENSE`. The original sxhkd code by
Bastien Dejean is licensed under the BSD 2-Clause license, reproduced in
`LICENSE.BSD-2-Clause` and in the headers of his files.

----

For further information, check the `man` pages.
