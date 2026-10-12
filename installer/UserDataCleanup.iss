// An elevated uninstaller may run as a different account. Never offer to
// remove that administrator's settings on behalf of the interactive user.
type
  TCleanupTokenUser = record
    Sid: NativeUInt;
    Storage: array[0..255] of Byte;
  end;

function CleanupGetShellWindow(): HWND;
  external 'GetShellWindow@user32.dll stdcall';
function CleanupWindowProcess(Window: HWND; var ProcessId: LongWord): LongWord;
  external 'GetWindowThreadProcessId@user32.dll stdcall';
function CleanupOpenProcess(Access: LongWord; Inherit: BOOL; ProcessId: LongWord): THandle;
  external 'OpenProcess@kernel32.dll stdcall';
function CleanupCurrentProcess(): THandle;
  external 'GetCurrentProcess@kernel32.dll stdcall';
function CleanupOpenToken(Process: THandle; Access: LongWord; var Token: THandle): BOOL;
  external 'OpenProcessToken@advapi32.dll stdcall';
function CleanupTokenUser(Token: THandle; InfoClass: Integer;
  var Info: TCleanupTokenUser; InfoLength: LongWord; var Required: LongWord): BOOL;
  external 'GetTokenInformation@advapi32.dll stdcall';
function CleanupEqualSid(First, Second: NativeUInt): BOOL;
  external 'EqualSid@advapi32.dll stdcall';
function CleanupCloseHandle(Handle: THandle): BOOL;
  external 'CloseHandle@kernel32.dll stdcall';
function CleanupFileAttributes(Name: String): LongWord;
  external 'GetFileAttributesW@kernel32.dll stdcall';

function IsInteractiveUserDataOwner(): Boolean;
var
  ShellWindow: HWND;
  ProcessId, Required: LongWord;
  ShellProcess, ShellToken, CurrentToken: THandle;
  ShellUser, CurrentUser: TCleanupTokenUser;
begin
  Result := False;
  ShellWindow := CleanupGetShellWindow();
  if ShellWindow = 0 then Exit;
  ProcessId := 0;
  CleanupWindowProcess(ShellWindow, ProcessId);
  if ProcessId = 0 then Exit;
  ShellProcess := CleanupOpenProcess($1000, False, ProcessId);
  if ShellProcess = 0 then Exit;
  try
    if not CleanupOpenToken(ShellProcess, $0008, ShellToken) then Exit;
    try
      if not CleanupOpenToken(CleanupCurrentProcess(), $0008, CurrentToken) then Exit;
      try
        if not CleanupTokenUser(ShellToken, 1, ShellUser, SizeOf(ShellUser), Required) then Exit;
        if not CleanupTokenUser(CurrentToken, 1, CurrentUser, SizeOf(CurrentUser), Required) then Exit;
        Result := CleanupEqualSid(ShellUser.Sid, CurrentUser.Sid);
      finally
        CleanupCloseHandle(CurrentToken);
      end;
    finally
      CleanupCloseHandle(ShellToken);
    end;
  finally
    CleanupCloseHandle(ShellProcess);
  end;
end;

function IsUnredirectedDirectory(Directory: String): Boolean;
var
  Parent: String;
  Attributes: LongWord;
begin
  Result := False;
  // Cleanup is optional. Keep data if any part of its path is redirected.
  while Directory <> '' do
  begin
    Attributes := CleanupFileAttributes(Directory);
    if (Attributes = $FFFFFFFF) or ((Attributes and $10) = 0) or
       ((Attributes and $400) <> 0) then Exit;
    Parent := ExtractFileDir(Directory);
    if (Parent = Directory) or (Length(Directory) <= 3) then
    begin
      Result := True;
      Exit;
    end;
    Directory := Parent;
  end;
end;

function CanRemoveUserData(const Directory: String): Boolean;
var
  Expected: String;
begin
  Result := False;
  Expected := ExpandConstant('{localappdata}\LowLatencyCaptureViewer');
  if CompareText(Directory, Expected) <> 0 then Exit;
  if not IsInteractiveUserDataOwner() then Exit;
  Result := IsUnredirectedDirectory(Directory);
end;
