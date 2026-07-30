Add-Type -AssemblyName System.Security

function To-Hex([byte[]]$bytes) {
    return (($bytes | ForEach-Object { $_.ToString("x2") }) -join "")
}

function From-Hex([string]$hex) {
    $hex = $hex -replace '\s+', ''
    $bytes = New-Object byte[] ($hex.Length / 2)

    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
    }

    return $bytes
}

function Sha256([byte[]]$bytes) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return $sha.ComputeHash($bytes)
    }
    finally {
        $sha.Dispose()
    }
}

function ConcatBytes([byte[][]]$arrays) {
    $len = 0

    foreach ($a in $arrays) {
        if ($null -ne $a) {
            $len += $a.Length
        }
    }

    $out = New-Object byte[] $len
    $pos = 0

    foreach ($a in $arrays) {
        if ($null -eq $a) {
            continue
        }

        [Array]::Copy($a, 0, $out, $pos, $a.Length)
        $pos += $a.Length
    }

    return $out
}

$cachePath = Read-Host "Enter the full path to Goodix_Cache.bin"

if (!(Test-Path $cachePath)) {
    Write-Host "File not found."
    exit 1
}

$full = [System.IO.File]::ReadAllBytes($cachePath)

if ($full.Length -lt 16) {
    Write-Host "The file is invalid or too small."
    exit 1
}

$minus8 = $full[0..($full.Length - 9)]
$last8 = $full[($full.Length - 8)..($full.Length - 1)]

$tableA = From-Hex "9d7992b38402b66c81d1f555218942a9"
$tableB = From-Hex "1848d71550d270d219c80632ab4f8bb3"
$tableC = From-Hex "e47c8938db5250f0205617ee17da4eb4"

$key16 = New-Object byte[] 16

for ($i = 0; $i -lt 8; $i++) {
    $key16[$i] = $tableA[$i] -bxor $tableB[$i] -bxor $tableA[$i + 8]
}

for ($i = 8; $i -lt 16; $i++) {
    $key16[$i] = $tableB[$i] -bxor $tableC[$i] -bxor $tableC[$i - 8]
}

$hLast8 = Sha256 $last8

$entropy = ConcatBytes @(
    $hLast8[16..31],
    (Sha256 (ConcatBytes @($hLast8[0..15], $key16)))
)

$dataVariants = @(
    [PSCustomObject]@{ Name = "full"; Bytes = $full },
    [PSCustomObject]@{ Name = "minus8"; Bytes = $minus8 }
)

$scopes = @(
    [System.Security.Cryptography.DataProtectionScope]::CurrentUser,
    [System.Security.Cryptography.DataProtectionScope]::LocalMachine
)

$psk = $null

foreach ($data in $dataVariants) {
    foreach ($scope in $scopes) {
        try {
            $plain = [System.Security.Cryptography.ProtectedData]::Unprotect(
                $data.Bytes,
                $entropy,
                $scope
            )

            if ($plain.Length -eq 32) {
                $psk = To-Hex $plain
                break
            }
        }
        catch {
        }
    }

    if ($null -ne $psk) {
        break
    }
}

if ($null -eq $psk) {
    Write-Host "The PSK could not be extracted."
    exit 2
}

$outPath = Join-Path ([Environment]::GetFolderPath("Desktop")) "goodix_psk.txt"
Set-Content -Path $outPath -Value $psk -Encoding ASCII

Write-Host "Completed: $outPath"
