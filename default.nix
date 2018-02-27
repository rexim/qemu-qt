with import <nixpkgs> {}; {
    qemuQtEnvEnv = stdenv.mkDerivation {
        name = "qemuQtEnvEnv";
        buildInputs = [ stdenv cmake libvncserver qt48Full ];
    };
}
