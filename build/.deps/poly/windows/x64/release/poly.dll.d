{
    files = {
        [[build\.objs\poly\windows\x64\release\src\main.cpp.obj]]
    },
    values = {
        [[C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\link.exe]],
        {
            "-nologo",
            "-machine:x64",
            [[-libpath:C:\Users\Administrator\AppData\Local\.xmake\packages\m\minhook\v1.3.4\42db4578518a4e7dab516663d1f46c6b\lib]],
            "/opt:ref",
            "/opt:icf",
            "minhook.lib",
            "user32.lib",
            "gdi32.lib",
            "comctl32.lib",
            "comdlg32.lib",
            "shell32.lib",
            "ole32.lib"
        }
    }
}