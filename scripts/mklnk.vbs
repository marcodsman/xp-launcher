Set ws = CreateObject("WScript.Shell")
target = "C:\XP_Share\launcher\launcher.exe"
wd = "C:\XP_Share\launcher"
For Each place In Array("AllUsersDesktop", "AllUsersStartup", "AllUsersStartMenu")
  Set lnk = ws.CreateShortcut(ws.SpecialFolders(place) & "\Entertainment System.lnk")
  lnk.TargetPath = target
  lnk.WorkingDirectory = wd
  lnk.Description = "Performa Entertainment System"
  lnk.Save
Next
WScript.Echo "shortcuts created"
