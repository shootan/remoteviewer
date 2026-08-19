#pragma once

// Resource ids shared between the generated payload script and the installer sources.
#define IDI_GNLINK 101

#define IDR_PAYLOAD_HOST_APP 200
#define IDR_PAYLOAD_VIDEO_HOST 201
#define IDR_PAYLOAD_SECURE_INPUT 202
#define IDR_PAYLOAD_GDI_WORKER 203
// The viewing half. One install puts both on the machine, because the PC you leave running is
// usually also one you sit at.
#define IDR_PAYLOAD_CLIENT_SHELL 204
#define IDR_PAYLOAD_CLIENT_VIEWER 205
// Not executables: the two pages the client and viewer draw their interfaces from, written into
// a ui subdirectory beside them.
#define IDR_PAYLOAD_CLIENT_UI 206
#define IDR_PAYLOAD_MACRO_UI 207

#define IDD_MAIN 300
#define IDC_APP_ICON 301
#define IDC_TITLE 302
#define IDC_STATUS 303
// The primary button changes meaning with what is already on the machine: Install, Update,
// or Repair. Uninstall only appears once there is something to remove.
#define IDC_PRIMARY 304
#define IDC_UNINSTALL 305
