{ pkgs ? import <nixos> {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    alsa-lib
    clang
    cmake
    gcc
    glibc
    jack2
    pipewire
    pkg-config
    portaudio
  ];
}
