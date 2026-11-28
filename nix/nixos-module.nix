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
    lib.optionalAttrs (cfg.backgroundRun != null) { backgroundRun = cfg.backgroundRun; }
    // lib.optionalAttrs (cfg.startAtLogin != null) { startAtLogin = cfg.startAtLogin; }
    // lib.optionalAttrs (cfg.listenPort != 48995) { listenPort = cfg.listenPort; }
    // lib.optionalAttrs (!cfg.lanDiscovery) { lanDiscovery = cfg.lanDiscovery; }
    // lib.optionalAttrs (cfg.defaultVaultsRoot != null) { defaultVaultsRoot = cfg.defaultVaultsRoot; }
    // lib.optionalAttrs (cfg.deviceName != null) { deviceName = cfg.deviceName; }
    // lib.optionalAttrs (cfg.identityCertFile != null) { identityCertFile = cfg.identityCertFile; }
    // lib.optionalAttrs (cfg.identityKeyFile != null) { identityKeyFile = cfg.identityKeyFile; }
    // lib.optionalAttrs (cfg.vaults != [ ]) { vaults = map renderVault cfg.vaults; };
in
{
  options.services.silex = {
    enable = lib.mkEnableOption "Silex desktop package, firewall rules, and optional system-wide managed config";
    package = lib.mkOption { type = lib.types.package; default = self.packages.${system}.default; };
    openFirewall = lib.mkOption { type = lib.types.bool; default = true; };
    listenPort = lib.mkOption { type = lib.types.port; default = 48995; };
    lanDiscovery = lib.mkOption { type = lib.types.bool; default = true; };
    backgroundRun = lib.mkOption { type = lib.types.nullOr (lib.types.enum [ "auto" true false ]); default = null; };
    startAtLogin = lib.mkOption { type = lib.types.nullOr lib.types.bool; default = null; };
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

    networking.firewall = lib.mkIf cfg.openFirewall {
      allowedTCPPorts = [ cfg.listenPort ];
      allowedUDPPorts = lib.optionals cfg.lanDiscovery [ 48996 ];
    };

    environment.etc."silex/managed.json" = lib.mkIf (managedAttrs != { }) {
      text = builtins.toJSON managedAttrs;
    };
  };
}
