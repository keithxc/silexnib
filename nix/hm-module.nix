{ self }:
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.silex;
  system = pkgs.stdenv.hostPlatform.system;

  peerSubmodule = lib.types.submodule {
    options = {
      deviceId = lib.mkOption { type = lib.types.str; default = ""; };
      name = lib.mkOption { type = lib.types.str; default = ""; };
      platform = lib.mkOption { type = lib.types.str; default = "desktop"; };
      fingerprint = lib.mkOption { type = lib.types.str; description = "64-char SHA-256 leaf cert DER hex fingerprint."; };
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

  serviceExec = pkgs.writeShellScript "silex-background-service" ''
    if [ -n "''${WAYLAND_DISPLAY:-}" ] && [ -z "''${QT_QPA_PLATFORM:-}" ]; then
      export QT_QPA_PLATFORM=wayland
    fi
    exec ${cfg.package}/bin/silex --background-if-enabled
  '';
in
{
  options.programs.silex = {
    enable = lib.mkEnableOption "Silex pure-native Markdown, notebook, and mTLS sync app";
    installPackage = lib.mkOption { type = lib.types.bool; default = true; };
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
    home.packages = lib.mkIf cfg.installPackage [ cfg.package ];
    xdg.configFile."silex/managed.json".text = builtins.toJSON managedAttrs;

    systemd.user.services.silex = lib.mkIf (pkgs.stdenv.isLinux && cfg.startAtLogin && cfg.backgroundRun != false) {
      Unit = {
        Description = "Silex background vault sync";
        After = [ "graphical-session.target" ];
        PartOf = [ "graphical-session.target" ];
        StartLimitIntervalSec = 0;
      };
      Service = {
        Type = "simple";
        ExecStart = "${serviceExec}";
        Restart = "on-failure";
        RestartSec = 5;
        KillMode = "control-group";
        TimeoutStopSec = 10;
      };
      Install.WantedBy = [ "graphical-session.target" ];
    };
  };
}
