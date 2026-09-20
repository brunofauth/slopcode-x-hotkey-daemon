# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "sxhkd - Simple X hotkey daemon, with an on-screen chord-chain indicator";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      # sxhkd is an X11 daemon, so only Linux hosts make sense.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      version = nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);

      # What the daemon links: xcb only, found by the compiler without
      # pkg-config. The indicator adds the drawing libraries, which the
      # Makefile locates through pkg-config.
      xcbInputs = pkgs: [
        pkgs.libxcb
        pkgs.libxcb-util           # xcb/xcb_event.h
        pkgs.libxcb-keysyms        # xcb/xcb_keysyms.h
      ];
      drawingInputs = pkgs: [
        pkgs.cairo                 # cairo, cairo-xcb
        pkgs.pango                 # pango, pangocairo
      ];

      # One derivation per program, both from this one source tree: `make
      # PROGRAMS=name` builds and installs that program and its man page only
      # (the examples go in with the daemon).
      mkProgram = pkgs: { name, description, nativeBuildInputs, buildInputs, doCheck }:
        pkgs.stdenv.mkDerivation {
          pname = name;
          inherit version;
          src = self;

          inherit nativeBuildInputs buildInputs;

          # The Makefile falls back to the VERSION file when `git describe` fails,
          # which it does inside the sandbox; make that explicit.
          makeFlags = [ "PREFIX=${placeholder "out"}" "VERCMD=false" "PROGRAMS=${name}" ];

          # Runs the headless unit tests (indicator core, cli, options, protocol).
          # UBSan only: the sanitizer runtimes of ASan and glibc's fortified
          # functions, which the Nix compiler wrapper enables, do not cooperate.
          # The dev shell below disables fortification so that `make check`
          # there runs ASan as well. -Werror is opt-in in the Makefile; nixpkgs
          # pins the compiler, so it is safe to turn on here and keeps this
          # CI-style build strict.
          inherit doCheck;
          checkTarget = "check";
          checkFlags = [ "TEST_CFLAGS=-fsanitize=undefined" "WERROR=-Werror" ];

          meta = with pkgs.lib; {
            inherit description;
            homepage = "https://github.com/baskerville/sxhkd";
            license = with licenses; [ gpl3Plus bsd2 ];
            platforms = supportedSystems;
            mainProgram = name;
          };
        };

      mkSxhkd = pkgs: mkProgram pkgs {
        name = "sxhkd";
        description = "Simple X hotkey daemon";
        nativeBuildInputs = [ ];
        buildInputs = xcbInputs pkgs;
        # Every unit test is free of X and of the drawing libraries, so the
        # whole suite, the indicator's tests included, runs here.
        doCheck = true;
      };

      mkSxhkdIndicator = pkgs: mkProgram pkgs {
        name = "sxhkd-indicator";
        description = "On-screen chord-chain indicator for sxhkd";
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = xcbInputs pkgs ++ drawingInputs pkgs;
        # The suite already ran in the daemon's derivation (see above).
        doCheck = false;
      };
    in
    {
      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in {
          sxhkd = mkSxhkd pkgs;
          sxhkd-indicator = mkSxhkdIndicator pkgs;
          default = self.packages.${system}.sxhkd;
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.sxhkd}/bin/sxhkd";
        };
      });

      checks = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          inherit (self.packages.${system}) sxhkd sxhkd-indicator;
        in {
          # `nix flake check` builds both programs; the daemon's build runs
          # the unit tests.
          build-sxhkd = sxhkd;
          build-sxhkd-indicator = sxhkd-indicator;
          # Strict warnings, -fanalyzer and cppcheck over the indicator, cli
          # and option sources. indicator.c is among them, so this starts from
          # the indicator's derivation, which has pkg-config and the drawing
          # libraries.
          analyze = sxhkd-indicator.overrideAttrs (previous: {
            pname = "sxhkd-analyze";
            nativeBuildInputs = previous.nativeBuildInputs ++ [ pkgs.cppcheck ];
            buildPhase = "make analyze";
            doCheck = false;
            installPhase = "touch $out";
          });
        });

      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in {
          default = pkgs.mkShell {
            # Both programs: the shell can build, test and analyze everything.
            inputsFrom = [
              self.packages.${system}.sxhkd
              self.packages.${system}.sxhkd-indicator
            ];
            packages = [
              pkgs.cppcheck
              pkgs.clang-tools        # clang-tidy for `make analyze`
              pkgs.valgrind
              pkgs.asciidoc           # a2x for `make doc`
              pkgs.xorg-server        # Xephyr for manual testing
              pkgs.xwininfo
              pkgs.xprop
              pkgs.xdotool
              pkgs.xterm
            ];
            # Let `make check` run ASan + UBSan (see checkFlags above).
            hardeningDisable = [ "fortify" ];
          };
        });
    };
}
