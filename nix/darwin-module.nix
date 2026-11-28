{ self }:
{ config, lib, pkgs, ... }:

let
  cfg = config.services.silex;
  system = pkgs.stdenv.hostPlatform.system;

  peerSubmodule = lib.types.submodule {
    options = {
      deviceId = lib.mkOption { type = lib.types.str; default = ""; };
      name = lib.mkOption { type = lib.types.str; default = ""; };
      platform = lib.mkOption { type = lib.types.str; default = "desktop"; };
      fingerprint = lib.mkOption { type = lib.types.str; };
      addresses = lib.mkOption { type = lib.types.listOf lib.types.str; default = [ ]; };
    };
  };

  vaultSubmodule = lib.types.submodule {
    options = {
      vaultId = lib.mkOption { type = lib.types.str; description = "Shared vault sync ID across devices."; };
      path = lib.mkOption { type = lib.types.str; description = "Local vault folder path on this host."; };
      peers = lib.mkOption { type = lib.types.listOf peerSubmodule; default = [ ]; };
    };
  };

  renderPeer = p: { inherit (p) deviceId name platform fingerprint addresses; };
  renderVault = v: {
    inherit (v) vaultId path;
    peers = map renderPeer v.peers;
  };

  managedAttrs =
    {
      backgroundRun = cfg.backgroundRun;
      startAtLogin = cfg.startAtLogin;
      listenPort = cfg.listenPort;
      lanDiscovery = cfg.lanDiscovery;
    }
    // lib.optionalAttrs (cfg.defaultVaultsRoot != null) { defaultVaultsRoot = cfg.defaultVaultsRoot; }
    // lib.optionalAttrs (cfg.deviceName != null) { deviceName = cfg.deviceName; }
    // lib.optionalAttrs (cfg.identityCertFile != null) { identityCertFile = cfg.identityCertFile; }
    // lib.optionalAttrs (cfg.identityKeyFile != null) { identityKeyFile = cfg.identityKeyFile; }
    // lib.optionalAttrs (cfg.vaults != [ ]) { vaults = map renderVault cfg.vaults; };
in
{
  options.services.silex = {
    enable = lib.mkEnableOption "Silex macOS app bundle, declarative managed config, and LaunchAgent background sync";
    package = lib.mkOption { type = lib.types.package; default = self.packages.${system}.default; };
    backgroundRun = lib.mkOption { type = lib.types.enum [ "auto" true false ]; default = "auto"; };
    startAtLogin = lib.mkOption { type = lib.types.bool; default = true; };
    listenPort = lib.mkOption { type = lib.types.port; default = 48995; };
    lanDiscovery = lib.mkOption { type = lib.types.bool; default = true; };
    defaultVaultsRoot = lib.mkOption { type = lib.types.nullOr lib.types.str; default = null; };
    deviceName = lib.mkOption { type = lib.types.nullOr lib.types.str; default = null; };
    identityCertFile = lib.mkOption { type = lib.types.nullOr lib.types.str; default = null; };
    identityKeyFile = lib.mkOption { type = lib.types.nullOr lib.types.str; default = null; };
    vaults = lib.mkOption {
      type = lib.types.listOf vaultSubmodule;
      default = [ ];
      description = "Declaratively bound vaults and their per-vault sync circle peers (each host excludes itself automatically).";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    environment.etc."silex/managed.json".text = builtins.toJSON managedAttrs;

    launchd.user.agents.silex = lib.mkIf (cfg.startAtLogin && cfg.backgroundRun != false) {
      serviceConfig = {
        ProgramArguments = [
          "/Applications/Nix Apps/Silex.app/Contents/MacOS/silex"
          "--background-if-enabled"
        ];
        RunAtLoad = true;
        KeepAlive = { SuccessfulExit = false; };
        LimitLoadToSessionType = "Aqua";
        ProcessType = "Background";
        StandardOutPath = "/tmp/silex-bg.out.log";
        StandardErrorPath = "/tmp/silex-bg.err.log";
      };
    };
  };
}
