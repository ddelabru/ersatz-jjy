{ pkgs ? import <nixos> {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    alsa-lib
    clang
    cmake
    gcc
    glibc
    jack2
    pkg-config
    portaudio
  ];
}
