{ pkgs ? import <nixos> {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    alsa-lib
    gcc
    glibc
    jack2
    pkg-config
    portaudio
  ];
}
