#!/bin/sh
#
# BigFix installer for The Chronicles of Riddick: Escape From Butcher Bay,
# for Linux, Steam Deck (SteamOS) and every other Proton/Wine user.
#
# It mirrors installer/install.ps1: the same System/Win64_AMD64 layout, the same
# .bigfix-backup backups, the same .bigfix-manifest.json and the same
# Environment.cfg merge, so a game folder installed by either script can be
# uninstalled by the other.
#
#   sh install.sh --game-dir DIR [--prefix WINEPREFIX] [--uninstall]
#                 [--dry-run] [--print-steam-options]
#                 [--source-dir DIR] [--data-file FILE] [--desktop-mode WxH@HZ]
#
# POSIX sh only (dash, busybox ash, bash): no arrays, no [[, no pipefail.
# Under Wine/Proton the ASI loader (winmm.dll) only loads when winmm is set to
# native first, so the script sets the override in the prefix, or prints the
# exact Steam launch option when it cannot.

set -eu

BACKUP_DIR_NAME='.bigfix-backup'
MANIFEST_NAME='.bigfix-manifest.json'
SYSTEM_REL='System/Win64_AMD64'
ENGINE_REL='System/Win64_AMD64/SbzEngine.exe'
CONFIG_NAME='Environment.cfg'
STEAM_LAUNCH_OPTION='WINEDLLOVERRIDES="winmm=n,b" %command%'
DECK_MODE_WIDTH=1280
DECK_MODE_HEIGHT=800
DECK_MODE_HZ=60
TAB=$(printf '\t')

usage() {
    cat <<'EOF'
Install or remove BigFix in The Chronicles of Riddick: Escape From Butcher Bay.

Options:
  --game-dir DIR        The game folder (the one holding System/Win64_AMD64).
                        Required: Linux has no registry to look it up in.
  --prefix WINEPREFIX   Wine/Proton prefix to set the winmm override in. Needs
                        wine on PATH; otherwise the launch option is printed.
  --uninstall           Reverse a previous install using .bigfix-manifest.json.
  --dry-run             Report what would change and change nothing.
  --print-steam-options Print the Steam launch option and exit.
  --source-dir DIR      Folder holding BigFix.asi, BigFix.ini and winmm.dll.
                        Default: this script's folder.
  --audio-backend NAME  system (default), or dsoal (requires Python 3).
  --backup-audio-conflicts Back up existing audio wrappers before replacement.
  --data-file FILE      Fingerprint data. Default: bigfix-data.json next to this
                        script when python3 is available, else bigfix-data.txt.
  --desktop-mode WxH@HZ Desktop mode to write into Environment.cfg, e.g.
                        2560x1440@180. Default: ask xrandr, else 1280x800@60
                        (the Steam Deck's panel).
  -h, --help            Show this help.

Steam / Steam Deck (Desktop Mode):
  Add System/Win64_AMD64/SbzEngine.exe as a non-Steam shortcut, set Start In
  to System/Win64_AMD64, and select a Proton version under Compatibility.
  Launch Options: WINEDLLOVERRIDES="winmm=n,b" %command%
  A typical Deck prefix is:
    ~/.local/share/Steam/steamapps/compatdata/<shortcut-id>/pfx
  Games inside that prefix are under pfx/drive_c; an SD card or a custom
  library has a different path. Pass the actual game folder to --game-dir.
  --prefix takes the pfx folder, not compatdata/<shortcut-id> or drive_c.
  Proton users can use Launch Options without a system wine installation.

Exit status: 0 on success, 1 on error, 2 when the game is 1.0 (EU10).
EOF
}

fail() {
    printf 'error: %s\n' "$1" >&2
    exit 1
}

warn() {
    printf 'warning: %s\n' "$1" >&2
}

note() {
    printf '%s\n' "$1"
}

# ------------------------------------------------------------------- paths
# Wine and Windows users type C:\Games\Riddick, so accept backslashes too.

normalize_path() {
    printf '%s' "$1" | tr '\\' '/'
}

abspath() {
    _abspath_path=$(normalize_path "$1")
    case $_abspath_path in
        /* | [A-Za-z]:/*) ;;
        *) _abspath_path=$(CDPATH= cd -- "$_abspath_path" 2>/dev/null && pwd -P) || return 1 ;;
    esac
    # Strip a trailing slash, but never the whole path.
    while :; do
        case $_abspath_path in
            ?*/) _abspath_path=${_abspath_path%/} ;;
            *) break ;;
        esac
    done
    printf '%s' "$_abspath_path"
}

json_string() {
    printf '"%s"' "$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')"
}

file_sha256() {
    _sha_output=$(sha256sum -- "$1")
    printf '%s' "${_sha_output%% *}"
}

# ---------------------------------------------------------- fingerprint data
# Both data files become the same internal table, one line per record:
#   <build id><TAB><relative path><TAB><sha256>
#   install<TAB><file name>

PYTHON=''
for _candidate in python3 python; do
    if command -v "$_candidate" >/dev/null 2>&1 &&
        "$_candidate" -c 'import json' >/dev/null 2>&1; then
        PYTHON=$_candidate
        break
    fi
done

read_data_file() {
    # $1: the data file. JSON needs python (jq is not assumed anywhere);
    # bigfix-data.txt is the flat fallback that gen_data.py also writes.
    case $1 in
        *.txt) cat -- "$1" ;;
        *)
            [ -n "$PYTHON" ] || fail "reading $1 needs python3; pass a .txt data file (bigfix-data.txt) with --data-file"
            "$PYTHON" - "$1" <<'PY'
import json
import sys

sys.stdout.reconfigure(newline="\n")
with open(sys.argv[1], encoding="utf-8-sig") as handle:
    data = json.load(handle)
for build in data["builds"]:
    for name in sorted(build["files"]):
        sys.stdout.write("%s\t%s\t%s\n" % (build["id"], name, build["files"][name].lower()))
for name in data.get("patchedFiles", []):
    sys.stdout.write("required\t%s\n" % name)
for build in data["builds"]:
    if build.get("supported") is True:
        sys.stdout.write("supported\t%s\n" % build["id"])
for name in data["install"]:
    sys.stdout.write("install\t%s\n" % name)
audio = data.get("components", {}).get("dsoal", {})
for entry in audio.get("files", []):
    print("audio_file\t%s\t%s\t%s" % (entry["name"], entry["sha256"], ",".join(entry["exports"])))
for key, value in audio.get("environment", {}).items():
    print("audio_env\t%s\t%s" % (key, value))
PY
            ;;
    esac
}

find_build() {
    # $1: game folder, $2: data table. Prints the matching build id, or nothing.
    _hash_table=$tmp_dir/hashes.tsv
    : >"$_hash_table"
    while IFS="$TAB" read -r _id _relative _expected; do
        if [ "$_id" = required ] || [ "$_id" = supported ]; then
            printf '%s\t%s\n' "$_id" "$_relative" >>"$_hash_table"
            continue
        fi
        if [ "$_id" = install ] || [ "$_id" = audio_file ] || [ "$_id" = audio_env ] || [ -z "${_relative:-}" ]; then
            continue
        fi
        _actual=''
        if [ -f "$1/$_relative" ]; then
            _actual=$(file_sha256 "$1/$_relative") || _actual=unreadable
        fi
        printf '%s\t%s\t%s\t%s\n' "$_id" "$_relative" "$_expected" "$_actual" >>"$_hash_table"
    done <"$2"

    # The policy comes from gen_data.py: all required files must match ONE
    # supported build. Never combine individually known files from two builds.
    awk -F "$TAB" '
        $1 == "required" { required[$2] = 1; next }
        $1 == "supported" { supported[$2] = 1; next }
        {
            id = $1
            if (!(id in position)) { position[id] = ++count; order[count] = id }
            if (length($3) == 64 && $3 !~ /[^0-9a-fA-F]/ && tolower($3) == tolower($4)) {
                matches[id]++; good[id SUBSEP $2] = 1
            } else {
                bad[id] = 1; different[id] = different[id] "  differs or missing: " $2 "\n"
            }
        }
        END {
            best = ""
            best_count = -1
            policy = 0; for (path in required) policy++
            if (!policy) {
                print "Fingerprint data has no required safety policy; update BigFix. No files were changed." > "/dev/stderr"
                exit 1
            }
            for (i = 1; i <= count; i++) {
                id = order[i]
                safe = (id in supported); unverified = ""
                for (path in required) if (!((id SUBSEP path) in good)) {
                    safe = 0; unverified = unverified " " path
                }
                if (safe || (id == "EU10" && !(id in bad))) {
                    if (matches[id] > best_count) { best = id; best_count = matches[id] }
                } else if (id in supported) {
                    print "Against " id ", unverified or missing patched files:" unverified > "/dev/stderr"
                }
            }
            if (best != "") {
                print best
                if (best in bad) printf "Build: %s (partial match; every patched file matches).\n%s", best, different[best] > "/dev/stderr"
            }
        }
    ' "$_hash_table"
}

# ------------------------------------------------------------ Environment.cfg

merge_environment_cfg() {
    # $1: existing file (or /dev/null), $2..$4: width height hz. Writes the
    # merged file to stdout, with the same rules as install.ps1:
    #   - managed keys are replaced in place, case-insensitively, uppercased;
    #   - missing managed keys are appended in a fixed order;
    #   - the existing line ending (CRLF or LF) and trailing newline survive.
    # A new file gets CRLF, like install.ps1; an existing file keeps the line
    # ending it already uses. Count carriage returns as bytes: grep on some
    # hosts strips CR on read, so `grep -q "$CR$"` would never match there.
    _cfg_crlf=1
    _cfg_trailing=1
    if [ -f "$1" ] && [ -s "$1" ]; then
        _cfg_crs=$(tr -cd '\r' <"$1" | wc -c | tr -d ' ')
        if [ "$_cfg_crs" -eq 0 ]; then
            _cfg_crlf=0
        fi
        if [ -n "$(tail -c 1 "$1")" ]; then
            _cfg_trailing=0
        fi
    fi
    awk -v W="$2" -v H="$3" -v HZ="$4" -v audio="$audio_backend" -v envfile="$_data_table" -v crlf="$_cfg_crlf" -v trailing="$_cfg_trailing" '
        function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
        function emit(s) { out[++count] = s }
        BEGIN {
            ORS = crlf ? "\r\n" : "\n"
            value["VID_MODE"] = W " " H " 32 " HZ
            value["VID_DWIDTH"] = W
            value["VID_DHEIGHT"] = H
            value["XR_SHADERMODE"] = "5"
            value["VID_VSYNC"] = "1"
            keys[1] = "VID_MODE"
            keys[2] = "VID_DWIDTH"
            keys[3] = "VID_DHEIGHT"
            keys[4] = "XR_SHADERMODE"
            keys[5] = "VID_VSYNC"
            keycount = 5
            if (audio == "dsoal") {
                while ((getline record < envfile) > 0) {
                    split(record, fields, "\t")
                    if (fields[1] == "audio_env") {
                        keys[++keycount] = fields[2]; value[fields[2]] = fields[3]
                    }
                }
                close(envfile)
            }
            count = 0
        }
        {
            line = $0
            if (crlf == 1 && substr(line, length(line)) == "\r") line = substr(line, 1, length(line) - 1)
            replaced = 0
            if (line ~ /^[ \t]*[^= \t#;][^=]*=/) {
                key = trim(substr(line, 1, index(line, "=") - 1))
                upper = toupper(key)
                if (upper in value) {
                    emit(upper "=" value[upper])
                    seen[upper] = 1
                    replaced = 1
                }
            }
            if (!replaced) emit(line)
        }
        END {
            for (i = 1; i <= keycount; i++) if (!(keys[i] in seen)) emit(keys[i] "=" value[keys[i]])
            for (i = 1; i <= count; i++) {
                if (i == count && trailing != 1) printf "%s", out[i]
                else printf "%s%s", out[i], ORS
            }
        }
    ' "$1"
}

desktop_mode() {
    # Prints "<width> <height> <hz>".
    if [ -n "$desktop_mode_arg" ]; then
        _mode_geometry=${desktop_mode_arg%@*}
        _mode_hz=${desktop_mode_arg##*@}
        if [ "$_mode_hz" = "$desktop_mode_arg" ]; then
            _mode_hz=60
        fi
        printf '%s %s %s\n' "${_mode_geometry%x*}" "${_mode_geometry#*x}" "$_mode_hz"
        return
    fi
    if command -v xrandr >/dev/null 2>&1; then
        _xrandr_line=$(xrandr 2>/dev/null | grep -m1 '\*' || true)
        _mode_geometry=$(printf '%s\n' "$_xrandr_line" |
            sed -n 's/^[[:space:]]*\([0-9][0-9]*x[0-9][0-9]*\).*/\1/p')
        _mode_rate=$(printf '%s\n' "$_xrandr_line" | tr -s ' ' '\n' | grep -m1 '\*' || true)
        if [ -n "$_mode_geometry" ] && [ -n "$_mode_rate" ]; then
            _mode_hz=$(printf '%s' "$_mode_rate" | sed 's/[^0-9.].*$//')
            _mode_hz=${_mode_hz%%.*}
            if [ -n "$_mode_hz" ]; then
                printf '%s %s %s\n' "${_mode_geometry%x*}" "${_mode_geometry#*x}" "$_mode_hz"
                return
            fi
        fi
    fi
    warn "Could not query the desktop resolution; using ${DECK_MODE_WIDTH}x${DECK_MODE_HEIGHT}@${DECK_MODE_HZ} (Steam Deck). Pass --desktop-mode to override."
    printf '%s %s %s\n' "$DECK_MODE_WIDTH" "$DECK_MODE_HEIGHT" "$DECK_MODE_HZ"
}

# ------------------------------------------------------------- the manifest

manifest_records() {
    # Prints "<path><TAB><backup>" for every file entry of an install.ps1 or
    # install.sh manifest. One property per line, as both writers emit them.
    # Windows paths come back forward-slashed, which every shell here accepts.
    awk '
        function value_after(line, key, rest) {
            if (!match(line, "\"" key "\"[ \t]*:[ \t]*")) return "\001"
            rest = substr(line, RSTART + RLENGTH)
            if (substr(rest, 1, 1) == "\"") {
                rest = substr(rest, 2)
                sub(/",?[ \t]*$/, "", rest)
            } else if (rest ~ /^null/) {
                return ""
            } else {
                sub(/[,} \t].*$/, "", rest)
            }
            gsub(/\\+/, "/", rest)
            return rest
        }
        {
            path = value_after($0, "path")
            if (path != "\001") current = path
            backup = value_after($0, "backup")
            if (backup != "\001") {
                if (current != "") printf "%s\t%s\n", current, backup
                current = ""
            }
        }
    ' "$1"
}

# ------------------------------------------------------------------ install

do_install() {
    game=$(abspath "$game_dir") || fail "Game folder '$game_dir' does not exist."
    [ -f "$game/$ENGINE_REL" ] || fail "Game folder '$game_dir' does not contain $ENGINE_REL."

    [ -f "$data_file" ] || fail "Data file not found: $data_file"
    [ -d "$source_dir" ] || fail "Source folder not found: $source_dir"

    # A data file edited on Windows may have CRLF line endings; the table is
    # always tab-separated LF so every field comparison stays exact.
    _data_table=$tmp_dir/data.tsv
    read_data_file "$data_file" >"$tmp_dir/data.raw"
    tr -d '\r' <"$tmp_dir/data.raw" >"$_data_table"

    _build=$(find_build "$game" "$_data_table")
    if [ -n "$_build" ]; then
        if [ "$_build" = EU10 ]; then
            note 'This is The Chronicles of Riddick 1.0 (EU DVD). BigFix needs version 1.1.'
            note 'Patch first: run riddick_update_eu_10_11.exe inside the same Wine/Proton'
            note 'prefix (wine riddick_update_eu_10_11.exe) or under Windows, then run this'
            note 'installer again.'
            note 'No files were changed.'
            exit 2
        fi
        note "Build: $_build."
    else
        warn 'unrecognised build: every patched file must match one verified build. No files were changed.'
        _tool=$script_dir/fingerprint.sh
        [ -f "$_tool" ] || _tool=$script_dir/../tools/fingerprint.sh
        # POSIX single-quote escaping keeps paths safe to paste into a shell.
        _quoted_game=$(printf '%s' "$game" | sed "s/'/'\\\\''/g")
        _quoted_tool=$(printf '%s' "$_tool" | sed "s/'/'\\\\''/g")
        note "Run from outside the game folder: sh '$_quoted_tool' --game-dir '$_quoted_game' --output 'bigfix-fingerprint.json'"
        note 'Attach bigfix-fingerprint.json to a new build issue using .github/ISSUE_TEMPLATE/new-build.yml. Do not upload game files.'
        exit 1
    fi

    _install_list=$tmp_dir/install.txt
    : >"$_install_list"
    while IFS="$TAB" read -r _id _name; do
        if [ "$_id" = install ] && [ -n "${_name:-}" ]; then
            printf '%s\n' "$_name" >>"$_install_list"
        fi
    done <"$_data_table"
    [ -s "$_install_list" ] || fail "Data file $data_file lists no files to install."

    if [ -f "$game/$MANIFEST_NAME" ]; then
        if [ "$audio_backend" = dsoal ] || grep -Eq '"audioBackend"[[:space:]]*:[[:space:]]*"dsoal"' "$game/$MANIFEST_NAME"; then
            fail 'Uninstall the current BigFix installation before changing the audio backend.'
        fi
    fi
    if [ "$audio_backend" = dsoal ]; then
        [ -n "$PYTHON" ] || fail 'DSOAL preflight requires Python 3; system audio installation does not.'
        [ "$(awk -F "$TAB" '$1 == "audio_file" {n++} END {print n+0}' "$_data_table")" -eq 2 ] || fail 'Missing pinned DSOAL component metadata.'
        "$PYTHON" "$script_dir/audio-payload.py" "$source_dir" "$_data_table" || fail 'Audio preflight failed; no files changed.'
        while IFS="$TAB" read -r _id _name _rest; do
            [ "$_id" = audio_file ] || continue
            [ ! -e "$game/$SYSTEM_REL/$BACKUP_DIR_NAME/$_name" ] || fail "Audio conflict: orphaned backup of $_name exists. Resolve it before installing."
            if [ -e "$game/$SYSTEM_REL/$_name" ] && [ "$backup_audio_conflicts" -eq 0 ]; then
                fail "Audio conflict: $_name already exists. Use --backup-audio-conflicts to back it up reversibly."
            fi
            printf '%s\n' "$_name" >>"$_install_list"
        done <"$_data_table"
        STEAM_LAUNCH_OPTION='WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%'
    fi

    _missing=''
    while IFS= read -r _name; do
        [ -n "$_name" ] || continue
        if [ -e "$game/$SYSTEM_REL/$_name" ] && [ ! -f "$game/$SYSTEM_REL/$_name" ]; then
            fail "Install destination is not a file: $_name"
        fi
        if [ ! -f "$source_dir/$_name" ]; then
            _missing="$_missing $_name"
        fi
    done <"$_install_list"
    if [ -n "$_missing" ]; then
        fail "Missing install file(s) in '$source_dir':$_missing"
    fi

    _system_dir=$game/$SYSTEM_REL
    _backup_dir=$_system_dir/$BACKUP_DIR_NAME
    # Snapshot before the first write. The exit trap restores an interrupted or
    # failed install; temporary snapshots are never stored in the game tree.
    if [ "$dry_run" -eq 0 ]; then
        : >"$tmp_dir/transaction.tsv"
        while IFS= read -r _name; do
            snapshot_file "$_system_dir/$_name"
            snapshot_file "$_backup_dir/$_name"
        done <"$_install_list"
        snapshot_file "$game/$CONFIG_NAME"
        snapshot_file "$_backup_dir/$CONFIG_NAME"
        snapshot_file "$game/$MANIFEST_NAME"
        transaction_active=1
    fi
    _files_json=''
    _first_entry=1
    while IFS= read -r _name; do
        [ -n "$_name" ] || continue
        _source=$source_dir/$_name
        _destination=$_system_dir/$_name
        _existed=false
        _backup_json=null
        if [ -f "$_destination" ]; then
            _existed=true
            _backup_path=$_backup_dir/$_name
            if [ -f "$_backup_path" ]; then
                note "  keeping the existing backup of $_name"
            elif [ "$dry_run" -eq 1 ]; then
                note "  dry run: would back up $_destination -> $_backup_path"
            else
                mkdir -p "$_backup_dir"
                cp -f "$_destination" "$_backup_path"
                note "  backed up $_name"
            fi
            _backup_json=$(json_string "$_backup_path")
        fi
        if [ "$dry_run" -eq 1 ]; then
            note "  dry run: would copy $_source -> $_destination"
        else
            cp -f "$_source" "$_destination"
            note "  installed $_name"
        fi
        if [ "$_first_entry" -eq 1 ]; then
            _separator=''
            _first_entry=0
        else
            _separator=',
'
        fi
        _files_json="$_files_json$_separator    {
      \"name\": $(json_string "$_name"),
      \"path\": $(json_string "$_destination"),
      \"existed\": $_existed,
      \"backup\": $_backup_json,
      \"installedSha256\": $(json_string "$(file_sha256 "$_source")")
    }"
    done <"$_install_list"

    _mode=$(desktop_mode)
    _mode_width=${_mode%% *}
    _mode_rest=${_mode#* }
    _mode_height=${_mode_rest%% *}
    _mode_hz=${_mode_rest#* }

    _cfg_path=$game/$CONFIG_NAME
    _cfg_existed=false
    _cfg_backup_json=null
    _cfg_backup_path=$_backup_dir/$CONFIG_NAME
    _cfg_merged=$tmp_dir/$CONFIG_NAME
    if [ -f "$_cfg_path" ]; then
        _cfg_existed=true
        _cfg_backup_json=$(json_string "$_cfg_backup_path")
        if [ "$dry_run" -eq 1 ]; then
            note "  dry run: would back up $_cfg_path -> $_cfg_backup_path"
        elif [ -f "$_cfg_backup_path" ]; then
            note "  keeping the existing backup of $CONFIG_NAME"
        else
            mkdir -p "$_backup_dir"
            cp -f "$_cfg_path" "$_cfg_backup_path"
        fi
        merge_environment_cfg "$_cfg_path" "$_mode_width" "$_mode_height" "$_mode_hz" >"$_cfg_merged"
    else
        merge_environment_cfg /dev/null "$_mode_width" "$_mode_height" "$_mode_hz" >"$_cfg_merged"
    fi
    if [ "$dry_run" -eq 1 ]; then
        note "  dry run: would write $_cfg_path with VID_MODE=$_mode_width $_mode_height 32 $_mode_hz, XR_SHADERMODE=5, VID_VSYNC=1"
    else
        cp -f "$_cfg_merged" "$_cfg_path"
        note "  wrote $CONFIG_NAME: VID_MODE=$_mode_width $_mode_height 32 $_mode_hz, XR_SHADERMODE=5, VID_VSYNC=1"
    fi

    if [ "$audio_backend" = dsoal ]; then note '  audio: SND_USE3D=1, SND_USEEAX=1 (restart required)'; fi

    if [ "$dry_run" -eq 1 ]; then
        apply_wine_override
        note "Dry run: no changes were made. BigFix would be installed into $_system_dir."
        print_launch_option
        return
    fi

    _build_json=null
    if [ -n "$_build" ]; then
        _build_json=$(json_string "$_build")
    fi
    {
        printf '{\n'
        printf '  "version": 1,\n'
        printf '  "audioBackend": %s,\n' "$(json_string "$audio_backend")"
        printf '  "gameDir": %s,\n' "$(json_string "$game")"
        printf '  "systemDir": %s,\n' "$(json_string "$_system_dir")"
        printf '  "build": %s,\n' "$_build_json"
        printf '  "desktopMode": %s,\n' "$(json_string "${_mode_width}x${_mode_height}@${_mode_hz}")"
        printf '  "files": [\n%s\n  ],\n' "$_files_json"
        printf '  "environmentCfg": {\n'
        printf '    "path": %s,\n' "$(json_string "$_cfg_path")"
        printf '    "existed": %s,\n' "$_cfg_existed"
        printf '    "backup": %s,\n' "$_cfg_backup_json"
        printf '    "installedSha256": %s\n' "$(json_string "$(file_sha256 "$_cfg_path")")"
        printf '  }\n'
        printf '}\n'
    } >"$game/$MANIFEST_NAME"
    transaction_active=0
    apply_wine_override

    note "BigFix installed into $_system_dir."
    print_launch_option
    note "Uninstall with: sh install.sh --uninstall --game-dir \"$game\""
}

apply_wine_override() {
    if [ -z "$wine_prefix" ]; then
        return
    fi
    if [ "$dry_run" -eq 1 ]; then
        note "  dry run: would set the winmm override in $wine_prefix with wine reg add"
        if [ "$audio_backend" = dsoal ]; then note "  dry run: would also set the dsound override in $wine_prefix"; fi
        return
    fi
    if ! command -v wine >/dev/null 2>&1; then
        warn "wine is not on PATH, so the winmm override was not set in $wine_prefix."
        return
    fi
    if WINEPREFIX="$wine_prefix" wine reg add 'HKCU\Software\Wine\DllOverrides' /v winmm /d native,builtin /f >/dev/null 2>&1; then
        note "  set the winmm override in $wine_prefix"
    else
        warn "wine reg add failed; set the override by hand or use the launch option below."
    fi
    if [ "$audio_backend" = dsoal ]; then
        if ! WINEPREFIX="$wine_prefix" wine reg add 'HKCU\Software\Wine\DllOverrides' /v dsound /d native,builtin /f >/dev/null 2>&1; then
            warn 'Could not set the dsound override; use the printed launch option.'
        fi
    fi
}

print_launch_option() {
    note ''
    note 'Wine/Proton only loads the ASI loader when winmm is set to native first.'
    note 'For Steam / Steam Deck, add a non-Steam shortcut with:'
    note "  Target: \"$game/$ENGINE_REL\""
    note "  Start In: \"$game/$SYSTEM_REL\""
    note 'Select a Proton version in Properties > Compatibility.'
    note "Steam launch option (Properties > General > Launch Options):"
    note "  $STEAM_LAUNCH_OPTION"
    note 'No BigFix.log next to the plugin means it did not load.'
}

# ---------------------------------------------------------------- uninstall

do_uninstall() {
    game=$(abspath "$game_dir") || fail "Game folder '$game_dir' does not exist."
    _manifest=$game/$MANIFEST_NAME
    [ -f "$_manifest" ] || fail "Nothing to uninstall: $_manifest does not exist."
    if grep -Eq '"audioBackend"[[:space:]]*:[[:space:]]*"dsoal"' "$_manifest"; then
        [ -n "$PYTHON" ] || fail 'Uninstalling DSOAL requires Python 3 for ownership checks.'
        "$PYTHON" "$script_dir/audio-payload.py" --check-uninstall "$_manifest" || fail 'Uninstall conflict; backups and manifest preserved.'
    fi

    _backup_dir=$game/$SYSTEM_REL/$BACKUP_DIR_NAME
    _records=$tmp_dir/records.tsv
    manifest_records "$_manifest" >"$_records"

    while IFS="$TAB" read -r _entry_path _entry_backup; do
        [ -n "${_entry_path:-}" ] || continue
        _destination=$(normalize_path "$_entry_path")
        _name=$(basename -- "$_destination")
        if [ "$dry_run" -eq 1 ]; then
            if [ -n "${_entry_backup:-}" ]; then
                note "  dry run: would restore $_destination from $_entry_backup"
            else
                note "  dry run: would remove $_destination"
            fi
            continue
        fi
        if [ -n "${_entry_backup:-}" ] && [ -f "$(normalize_path "$_entry_backup")" ]; then
            _source_path=$(normalize_path "$_entry_backup")
            cp -f "$_source_path" "$_destination"
            rm -f "$_source_path"
            note "  restored $_name"
        elif [ -f "$_destination" ]; then
            rm -f "$_destination"
            note "  removed $_name"
        fi
    done <"$_records"

    if [ "$dry_run" -eq 1 ]; then
        note 'Dry run: no changes were made.'
        return
    fi
    if [ -d "$_backup_dir" ] && [ -z "$(ls -A "$_backup_dir")" ]; then
        rmdir "$_backup_dir"
    fi
    rm -f "$_manifest"
    note "BigFix removed from $game."
}

# --------------------------------------------------------------------- main

game_dir=''
wine_prefix=''
source_dir=''
data_file=''
desktop_mode_arg=''
audio_backend=system
backup_audio_conflicts=0
transaction_active=0
snapshot_count=0
print_steam_options=0
uninstall=0
dry_run=0

while [ $# -gt 0 ]; do
    case $1 in
        --game-dir)
            [ $# -ge 2 ] || fail '--game-dir needs a value'
            game_dir=$2
            shift 2
            ;;
        --prefix)
            [ $# -ge 2 ] || fail '--prefix needs a value'
            wine_prefix=$2
            shift 2
            ;;
        --source-dir)
            [ $# -ge 2 ] || fail '--source-dir needs a value'
            source_dir=$2
            shift 2
            ;;
        --data-file)
            [ $# -ge 2 ] || fail '--data-file needs a value'
            data_file=$2
            shift 2
            ;;
        --desktop-mode)
            [ $# -ge 2 ] || fail '--desktop-mode needs a value'
            desktop_mode_arg=$2
            shift 2
            ;;
        --audio-backend)
            [ $# -ge 2 ] || fail '--audio-backend needs a value'
            audio_backend=$2
            case $audio_backend in system|dsoal) ;; *) fail 'audio backend must be system or dsoal' ;; esac
            shift 2
            ;;
        --backup-audio-conflicts)
            backup_audio_conflicts=1
            shift
            ;;
        --uninstall)
            uninstall=1
            shift
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        --print-steam-options)
            print_steam_options=1
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1 (try --help)"
            ;;
    esac
done

if [ "$print_steam_options" -eq 1 ]; then
    if [ -n "$game_dir" ]; then
        _print_game=$(normalize_path "$game_dir")
        if [ -f "$_print_game/$MANIFEST_NAME" ] && [ -f "$_print_game/$SYSTEM_REL/dsound.dll" ] && [ -f "$_print_game/$SYSTEM_REL/dsoal-aldrv.dll" ] &&
            grep -Eq '"audioBackend"[[:space:]]*:[[:space:]]*"dsoal"' "$_print_game/$MANIFEST_NAME"; then
            STEAM_LAUNCH_OPTION='WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%'
        fi
    fi
    printf '%s\n' "$STEAM_LAUNCH_OPTION"
    exit 0
fi

snapshot_file() {
    snapshot_count=$((snapshot_count+1))
    _snapshot_existed=0
    if [ -f "$1" ]; then
        cp "$1" "$tmp_dir/snapshot-$snapshot_count"
        _snapshot_existed=1
    fi
    printf '%s\t%s\t%s\n' "$1" "$snapshot_count" "$_snapshot_existed" >>"$tmp_dir/transaction.tsv"
}

cleanup() {
    _exit_status=$?
    trap - 0 HUP INT TERM
    if [ "$transaction_active" -eq 1 ]; then
        while IFS="$TAB" read -r _snapshot_path _snapshot_id _snapshot_existed; do
            if [ "$_snapshot_existed" -eq 1 ]; then
                if ! cmp -s "$tmp_dir/snapshot-$_snapshot_id" "$_snapshot_path"; then
                    cp "$tmp_dir/snapshot-$_snapshot_id" "$_snapshot_path"
                fi
            else
                rm -f "$_snapshot_path"
            fi
        done <"$tmp_dir/transaction.tsv"
        rmdir "$_backup_dir" 2>/dev/null || true
        warn 'Installation failed; restored the file transaction.'
    fi
    rm -rf "$tmp_dir"
    exit "$_exit_status"
}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/bigfix-install.XXXXXX") || fail 'cannot create a temporary folder'
trap cleanup 0
trap 'exit 1' HUP INT TERM

if [ -z "$game_dir" ]; then
    fail 'No game folder given. Pass --game-dir <path> to the folder holding System/Win64_AMD64/SbzEngine.exe.'
fi

# Validate everything that can be wrong before the first byte is written.
if [ -n "$desktop_mode_arg" ]; then
    printf '%s' "$desktop_mode_arg" | grep -Eq '^[0-9]+x[0-9]+(@[0-9]+)?$' ||
        fail "Invalid --desktop-mode '$desktop_mode_arg'; expected WIDTHxHEIGHT@HZ, e.g. 2560x1440@180."
fi
if [ "$uninstall" -eq 0 ] && [ -n "$wine_prefix" ]; then
    wine_prefix=$(abspath "$wine_prefix") || fail "Wine prefix '$wine_prefix' does not exist."
    [ -d "$wine_prefix" ] || fail "Wine prefix '$wine_prefix' is not a folder."
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if [ -z "$source_dir" ]; then
    source_dir=$script_dir
fi

if [ -z "$data_file" ]; then
    if [ -n "$PYTHON" ] && [ -f "$script_dir/bigfix-data.json" ]; then
        data_file=$script_dir/bigfix-data.json
    else
        data_file=$script_dir/bigfix-data.txt
    fi
fi

if [ "$uninstall" -eq 1 ]; then
    do_uninstall
else
    do_install
fi
