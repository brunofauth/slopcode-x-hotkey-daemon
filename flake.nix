{
  description = "sxhkd - Simple X hotkey daemon, with an on-screen chord-chain indicator";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      # sxhkd is an X11 daemon, so only Linux hosts make sense.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      version = nixpkgs.lib.removeSuffix "\n" (builtins.readFile ./VERSION);

      # Everything the build itself needs; the dev shell adds tooling on top.
      nativeBuildInputs = pkgs: [ pkgs.pkg-config ];
      buildInputs = pkgs: [
        pkgs.libxcb
        pkgs.libxcb-util           # xcb/xcb_event.h
        pkgs.libxcb-keysyms        # xcb/xcb_keysyms.h
        pkgs.cairo                 # cairo, cairo-xcb
        pkgs.pango                 # pango, pangocairo
      ];

      mkSxhkd = pkgs: pkgs.stdenv.mkDerivation {
        pname = "sxhkd";
        inherit version;
        src = self;

        nativeBuildInputs = nativeBuildInputs pkgs;
        buildInputs = buildInputs pkgs;

        # The Makefile falls back to the VERSION file when `git describe` fails,
        # which it does inside the sandbox; make that explicit.
        makeFlags = [ "PREFIX=${placeholder "out"}" "VERCMD=false" ];

        # Runs the headless unit tests (indicator core, options). UBSan only: the
        # sanitizer runtimes of ASan and glibc's fortified functions, which the
        # Nix compiler wrapper enables, do not cooperate. The dev shell below
        # disables fortification so that `make check` there runs ASan as well.
        doCheck = true;
        checkTarget = "check";
        checkFlags = [ "TEST_CFLAGS=-fsanitize=undefined" ];

        meta = with pkgs.lib; {
          description = "Simple X hotkey daemon";
          homepage = "https://github.com/baskerville/sxhkd";
          license = licenses.bsd2;
          platforms = supportedSystems;
          mainProgram = "sxhkd";
        };
      };
    in
    {
      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system}; in {
          sxhkd = mkSxhkd pkgs;
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
          sxhkd = self.packages.${system}.sxhkd;
        in {
          # `nix flake check` builds the package, which runs the unit test.
          build = sxhkd;
          # Strict warnings, -fanalyzer and cppcheck over the indicator sources.
          analyze = sxhkd.overrideAttrs (previous: {
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
            inputsFrom = [ self.packages.${system}.sxhkd ];
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
