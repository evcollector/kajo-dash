<#
Interactive menu for kajo.bat.

Draws header lines, section headings and numbered items, and moves a
highlighted cursor with Up/Down or W/S. Enter picks the highlighted item; an
item's number picks it straight away. Q, Esc or Ctrl+C quits, or goes back:
-QuitLabel names that last item, "Quit" unless a submenu says "Back".

Items with a page in -HelpFile show a [?] on the right. Right or D moves the
cursor onto it, Left or A back, and Enter there opens the page; any key
returns to the menu.

Each argument after the named ones is one line of the menu, in order:
  "=text"                     a header line
  "#text"                     a section heading
  "key|label|description"     an item; the key is a single digit

Exit code: 100 + the chosen digit, or 110 to quit. Any other code means the
menu could not run, and kajo.bat falls back to its plain numbered prompt.
Deliberately ASCII-only and PowerShell 5.1 compatible, like everything that
has to work on a fresh Windows install.
#>
param(
    [string]$Selected = "",
    [string]$HelpFile = "",
    [string]$QuitLabel = "Quit",
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Entries
)

$ErrorActionPreference = "Stop"
$QuitCode = 110
$LabelWidth = 22

# Help pages: "[n] Title", then paragraphs separated by blank lines; "- "
# starts a bullet and "#" a comment. See menu_help.txt.
$help = @{}
if ($HelpFile -and (Test-Path -LiteralPath $HelpFile)) {
    $page = $null
    foreach ($raw in [IO.File]::ReadAllLines($HelpFile)) {
        if ($raw.StartsWith("#")) { continue }
        if ($raw -match '^\[(\w)\]\s*(.*)$') {
            $page = @{ Title = $Matches[2]; Blocks = New-Object System.Collections.Generic.List[string] }
            $help[$Matches[1]] = $page
            continue
        }
        if ($null -eq $page) { continue }
        $line = $raw.Trim()
        if ($line -eq "") {
            if ($page.Blocks.Count -gt 0 -and $page.Blocks[$page.Blocks.Count - 1] -ne "") { $page.Blocks.Add("") }
        } elseif ($line.StartsWith("- ") -or $page.Blocks.Count -eq 0 -or $page.Blocks[$page.Blocks.Count - 1] -eq "") {
            $page.Blocks.Add($line)
        } else {
            $page.Blocks[$page.Blocks.Count - 1] += " " + $line
        }
    }
    foreach ($page in $help.Values) {
        while ($page.Blocks.Count -gt 0 -and $page.Blocks[$page.Blocks.Count - 1] -eq "") {
            $page.Blocks.RemoveAt($page.Blocks.Count - 1)
        }
    }
}

$lines = New-Object System.Collections.Generic.List[object]
$items = New-Object System.Collections.Generic.List[object]
foreach ($entry in $Entries) {
    if ($entry.StartsWith("=")) {
        $lines.Add(@{ Kind = "header"; Text = $entry.Substring(1) })
    } elseif ($entry.StartsWith("#")) {
        $lines.Add(@{ Kind = "blank" })
        $lines.Add(@{ Kind = "section"; Text = $entry.Substring(1) })
    } else {
        $parts = $entry.Split("|", 3)
        $item = @{ Kind = "item"; Key = $parts[0]; HasHelp = $help.ContainsKey($parts[0])
                   Text = ("{0}. {1}" -f $parts[0], $parts[1]).PadRight($LabelWidth + 3) + $parts[2] }
        $lines.Add($item)
        $items.Add($item)
    }
}
$quitItem = @{ Kind = "item"; Key = "Q"; Text = "Q. $QuitLabel"; HasHelp = $false }
$lines.Add(@{ Kind = "blank" })
$lines.Add($quitItem)
$items.Add($quitItem)
$barWidth = ($items | ForEach-Object { $_.Text.Length } | Measure-Object -Maximum).Maximum + 2

function Result-For($key) {
    if ($key -eq "Q") { return $QuitCode }
    return 100 + [int]$key
}

# Without a console to read keys from (input redirected), show the plain list
# and read a line instead, so the menu still works from scripts.
if ([Console]::IsInputRedirected) {
    foreach ($line in $lines) {
        switch ($line.Kind) {
            "header" { Write-Host "  $($line.Text)" }
            "section" { Write-Host "  $($line.Text)" }
            "blank" { Write-Host "" }
            "item" { Write-Host "    $($line.Text)" }
        }
    }
    $answer = [Console]::In.ReadLine()
    if ($null -eq $answer) { exit $QuitCode }
    $match = $items | Where-Object { $_.Key -eq $answer.Trim().ToUpperInvariant() } | Select-Object -First 1
    if ($match) { exit (Result-For $match.Key) }
    exit $QuitCode
}

function Show-Item($item, [bool]$current, [int]$column) {
    [Console]::SetCursorPosition(0, $item.Row)
    $onItem = $current -and $column -eq 0
    if ($onItem) {
        Write-Host "  > " -NoNewline -ForegroundColor DarkYellow
        Write-Host $item.Text.PadRight($barWidth) -NoNewline -ForegroundColor Black -BackgroundColor DarkYellow
    } else {
        Write-Host ("    " + $item.Text.PadRight($barWidth)) -NoNewline
    }
    if ($item.HasHelp) {
        Write-Host " " -NoNewline
        if ($current -and $column -eq 1) {
            Write-Host "[?]" -NoNewline -ForegroundColor Black -BackgroundColor DarkYellow
        } else {
            Write-Host "[?]" -NoNewline -ForegroundColor DarkGray
        }
    }
}

function Show-Menu {
    Clear-Host
    Write-Host ""
    $headerDone = $false
    foreach ($line in $lines) {
        if ($line.Kind -ne "header" -and -not $headerDone) {
            Write-Host "  =========================================="
            $headerDone = $true
        }
        switch ($line.Kind) {
            "header" { Write-Host "  $($line.Text)" }
            "section" { Write-Host "  $($line.Text)" -ForegroundColor DarkYellow }
            "blank" { Write-Host "" }
            "item" { $line.Row = [Console]::CursorTop; Show-Item $line $false 0; Write-Host "" }
        }
    }
    Write-Host ""
    $quitHint = "Q to " + $QuitLabel.ToLowerInvariant()
    if (@($items | Where-Object { $_.HasHelp }).Count -gt 0) {
        Write-Host "  Up/Down or W/S to move, Right or D for help, Enter or a number to choose," -ForegroundColor DarkGray
        Write-Host "  $quitHint" -ForegroundColor DarkGray
    } else {
        Write-Host "  Up/Down or W/S to move, Enter or a number to choose, $quitHint" -ForegroundColor DarkGray
    }
    $script:endRow = [Console]::CursorTop
}

function Wrap-Text([string]$text, [int]$width) {
    $result = New-Object System.Collections.Generic.List[string]
    $line = ""
    foreach ($word in $text.Split(" ")) {
        if ($line -eq "") { $line = $word }
        elseif (($line.Length + 1 + $word.Length) -le $width) { $line += " " + $word }
        else { $result.Add($line); $line = $word }
    }
    if ($line -ne "") { $result.Add($line) }
    return $result
}

function Show-Help($item) {
    $page = $help[$item.Key]
    $width = [Math]::Max(40, [Math]::Min(76, [Console]::WindowWidth - 6))
    Clear-Host
    Write-Host ""
    Write-Host ("  {0}. {1}" -f $item.Key, $page.Title) -ForegroundColor DarkYellow
    Write-Host "  =========================================="
    Write-Host ""
    foreach ($block in $page.Blocks) {
        if ($block -eq "") { Write-Host ""; continue }
        if ($block.StartsWith("- ")) {
            # @() keeps a one-line result a list; PowerShell would unwrap it to a string.
            $wrapped = @(Wrap-Text $block.Substring(2) ($width - 4))
            for ($i = 0; $i -lt $wrapped.Count; $i++) {
                $prefix = if ($i -eq 0) { "  - " } else { "    " }
                Write-Host ($prefix + $wrapped[$i])
            }
        } else {
            foreach ($row in (Wrap-Text $block ($width - 2))) { Write-Host "  $row" }
        }
    }
    Write-Host ""
    Write-Host "  Press any key to go back to the menu." -ForegroundColor DarkGray
    [void][Console]::ReadKey($true)
}

$index = 0
for ($i = 0; $i -lt $items.Count; $i++) {
    if ($items[$i].Key -eq $Selected) { $index = $i }
}
$column = 0

$result = $QuitCode
$cursorWasVisible = [Console]::CursorVisible
[Console]::CursorVisible = $false
[Console]::TreatControlCAsInput = $true
try {
    Show-Menu
    Show-Item $items[$index] $true $column
    while ($true) {
        $key = [Console]::ReadKey($true)
        $previous = $index
        $previousColumn = $column
        $char = [string]$key.KeyChar
        if ($key.Key -eq "UpArrow" -or $char -eq "w" -or $char -eq "W") {
            $index = ($index - 1 + $items.Count) % $items.Count
        } elseif ($key.Key -eq "DownArrow" -or $char -eq "s" -or $char -eq "S") {
            $index = ($index + 1) % $items.Count
        } elseif ($key.Key -eq "RightArrow" -or $char -eq "d" -or $char -eq "D") {
            if ($items[$index].HasHelp) { $column = 1 }
        } elseif ($key.Key -eq "LeftArrow" -or $char -eq "a" -or $char -eq "A") {
            $column = 0
        } elseif ($key.Key -eq "Home") {
            $index = 0
        } elseif ($key.Key -eq "End") {
            $index = $items.Count - 1
        } elseif ($key.Key -eq "Enter") {
            if ($column -eq 1) {
                Show-Help $items[$index]
                Show-Menu
                Show-Item $items[$index] $true $column
                continue
            }
            $result = Result-For $items[$index].Key
            break
        } elseif ($key.Key -eq "Escape" -or $char -eq "q" -or $char -eq "Q" -or
                  ($key.Key -eq "C" -and ($key.Modifiers -band [ConsoleModifiers]::Control))) {
            $result = $QuitCode
            break
        } else {
            $match = $items | Where-Object { $_.Key -eq $char } | Select-Object -First 1
            if ($match) {
                $result = Result-For $match.Key
                break
            }
        }
        # Moving onto a row without a help page puts the cursor back on the item.
        if (-not $items[$index].HasHelp) { $column = 0 }
        if ($index -ne $previous -or $column -ne $previousColumn) {
            Show-Item $items[$previous] $false 0
            Show-Item $items[$index] $true $column
        }
    }
} finally {
    [Console]::TreatControlCAsInput = $false
    [Console]::CursorVisible = $cursorWasVisible
    [Console]::SetCursorPosition(0, $script:endRow)
}
exit $result
