let
  sources = import ./nix/sources.nix;
  nixpkgs = import sources.nixpkgs {
    overlays = [ (self: super: { niv = (import sources.niv {}).niv; }) ];
  };
in
  with nixpkgs;
  stdenv.mkDerivation {
    name = "airplay-spec";
    src = ./.;
    buildInputs = [
      mdbook boot jdk12 niv
    ];
  }
