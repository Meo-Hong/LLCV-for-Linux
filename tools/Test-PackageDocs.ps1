param([Parameter(Mandatory = $true)][string]$PackageRoot)

# Offline check of the link forms used in the shipped Markdown: inline links
# and images, reference definitions, and HTML href/src attributes. Anchor text
# is left to the Markdown renderer; this check verifies packaged file targets.
# Plain-text launch/dependency notes are also checked for Markdown guide paths.
$ErrorActionPreference = "Stop"
$package = (Resolve-Path -LiteralPath $PackageRoot).Path.TrimEnd('\', '/')
$prefix = $package + [IO.Path]::DirectorySeparatorChar
$documents = @(Get-ChildItem -LiteralPath $package -File |
    Where-Object { $_.Extension -in @('.md', '.txt') })
foreach ($directory in @("docs", "tests")) {
    $path = Join-Path $package $directory
    if (Test-Path -LiteralPath $path -PathType Container) {
        $documents += @(Get-ChildItem -LiteralPath $path -File -Recurse |
            Where-Object { $_.Extension -in @('.md', '.txt') })
    }
}
if ($documents.Count -eq 0) { throw "No package documentation found: $package" }

$failures = [Collections.Generic.List[string]]::new()
$checkedLinks = 0
# Parentheses in unquoted destinations may be nested once (or escaped).
$destinationPattern = '(?:<(?<angle>[^>\r\n]+)>|(?<plain>(?:\\.|[^\s()\\]|\((?:\\.|[^\s()\\])*\))+))'
$patterns = @(
    ('\]\(\s*' + $destinationPattern + '(?:\s+"[^"\r\n]*"|\s+''[^''\r\n]*'')?\s*\)'),
    ('(?m)^ {0,3}\[[^\]\r\n]+\]:\s*' + $destinationPattern),
    '(?i)\b(?:href|src)\s*=\s*(?:"(?<double>[^"]*)"|''(?<single>[^'']*)'')'
)
# TXT notes use bare guide paths rather than Markdown links. Include quoted
# paths with spaces, either slash direction, external URLs, and Korean prose
# immediately following .md. Code/license source paths are not guide references.
$textPatterns = @(
    '(?i)(?:<(?<angle>[^>\r\n]+\.md(?:[?#][^>\r\n]*)?)>|"(?<double>[^"\r\n]+\.md(?:[?#][^"\r\n]*)?)"|(?<plain>[^\s<>"''`()\[\],;]+?\.md(?:[?#][^\s<>"''`()\[\],;]+)?))'
)
foreach ($document in $documents) {
    $relative = $document.FullName.Substring($prefix.Length).Replace('\', '/')
    $content = [IO.File]::ReadAllText($document.FullName)
    $documentPatterns = $patterns
    if ($document.Extension -eq '.txt') {
        $documentPatterns = $textPatterns
    } else {
        # Preserve line numbers while ignoring examples in comments and code blocks.
        $content = [regex]::Replace($content, '(?s)<!--.*?-->', {
            param($match) [regex]::Replace($match.Value, '[^\r\n]', ' ')
        })
        $lines = $content -split "`n"
        $fenceCharacter = ''
        $fenceLength = 0
        for ($i = 0; $i -lt $lines.Count; ++$i) {
            $fence = [regex]::Match($lines[$i], '^ {0,3}(?<fence>`{3,}|~{3,})')
            if ($fenceCharacter) {
                if ($fence.Success -and $fence.Groups['fence'].Value[0] -eq $fenceCharacter -and
                    $fence.Groups['fence'].Length -ge $fenceLength -and
                    $lines[$i].Substring($fence.Length).Trim().Length -eq 0) {
                    $fenceCharacter = ''
                }
                $lines[$i] = ' ' * $lines[$i].Length
            } elseif ($fence.Success) {
                $fenceCharacter = $fence.Groups['fence'].Value[0]
                $fenceLength = $fence.Groups['fence'].Length
                $lines[$i] = ' ' * $lines[$i].Length
            }
        }
        $content = $lines -join "`n"
        $content = [regex]::Replace($content, '(?s)(?<!`)(`+)(?!`).*?(?<!`)\1(?!`)', {
            param($match) [regex]::Replace($match.Value, '[^\r\n]', ' ')
        })
    }
    foreach ($pattern in $documentPatterns) {
        foreach ($match in [regex]::Matches($content, $pattern)) {
            $target = ''
            foreach ($group in @('angle', 'plain', 'double', 'single')) {
                if ($match.Groups[$group].Success) { $target = $match.Groups[$group].Value; break }
            }
            $target = [Net.WebUtility]::HtmlDecode($target)
            # External schemes, protocol-relative URLs, and same-page anchors
            # never require network access or a packaged file.
            $line = 1 + [regex]::Matches($content.Substring(0, $match.Index), "`n").Count
            if ($target -match '^(?:[A-Za-z]:[\\/]|file:)') {
                $failures.Add("${relative}:${line}: $target (absolute local path)")
                continue
            }
            if (-not $target -or $target.StartsWith('#') -or $target.StartsWith('//') -or
                $target -match '^[A-Za-z][A-Za-z0-9+.-]*:') { continue }
            $path = ($target -split '[?#]', 2)[0]
            $path = [Uri]::UnescapeDataString($path)
            $path = [regex]::Replace($path, '\\([()\[\] !])', '$1')
            if (-not $path) { continue }
            ++$checkedLinks
            try {
                # Absolute paths do not remain valid when a package is moved.
                if ([IO.Path]::IsPathRooted($path)) { throw "absolute local path" }
                $resolved = [IO.Path]::GetFullPath((Join-Path $document.DirectoryName $path))
                if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "path leaves the package"
                }
                if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "file not packaged" }
            } catch {
                $failures.Add("${relative}:${line}: $target ($($_.Exception.Message))")
            }
        }
    }
}
if ($failures.Count) {
    throw "Broken package documentation links ($($failures.Count)):`n$($failures -join "`n")"
}
[pscustomobject]@{ Documents = $documents.Count; LocalLinks = $checkedLinks }
