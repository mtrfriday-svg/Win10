# Push M'DEE to GitHub from Termux (no PC) – step by step

## Why `.github` sometimes goes missing
`.github` starts with a dot, so it is a **hidden folder**. It disappears when you:
* copy with `cp -r Bot2-main/* somewhere` (the `*` skips hidden names) – use `cp -a Bot2-main/. somewhere/` instead,
* move/zip the project with a phone file manager that hides dot-folders,
* add files with `git add *` (skips hidden) – use `git add -A`.

`termux_push.sh` fixes all of this: it re-creates `.github/workflows/mdee.yml` from the visible copy
`workflow_backup/mdee.yml`, force-adds it, pushes, and then prints **OK** only if the workflow is really in the commit.

## One-time setup
1. **Make a token** (GitHub app/browser → your photo → Settings → Developer settings → Personal access tokens →
   *Tokens (classic)* → Generate new token). Tick **repo** and **workflow**. Copy the token somewhere safe.
   (Fine-grained token instead? Give this repo *Contents: Read and write* and *Workflows: Read and write*.)
2. **Make an empty repo** on GitHub (New repository, name e.g. `mdee-robot`, do NOT add a README).
3. In Termux:
```
pkg update -y && pkg upgrade -y
pkg install -y git unzip
termux-setup-storage
```
   (Tap **Allow** on the permission popup.)
4. Put `Bot2-mdee.zip` in your phone's **Download** folder, then:
```
cd ~
cp ~/storage/downloads/Bot2-mdee.zip .
unzip -o Bot2-mdee.zip
cd Bot2-main
ls -a
```
   You must see `.github`, `.gitignore`, `termux_push.sh`, `main`, `mdee_body`. Then check:
```
ls .github/workflows
```
   It must print `mdee.yml`. (If not, don't worry, the next step recreates it.)

## Push (first time and every time)
```
cd ~/Bot2-main
bash termux_push.sh "first upload"
```
* It asks **GitHub username** and **Repository name** once.
* Then GitHub asks **Username** (your GitHub name) and **Password** → paste the **token** (not your password; nothing shows while pasting). Termux remembers it afterwards.
* At the end it must say `OK  The workflow is in the pushed commit.`

## Get the firmware
GitHub app/browser → your repo → **Actions** → *Build M'DEE robot* → latest run. When both jobs are green, open the run and
download **Artifacts**: `mdee_brain_n16r8_…` and `mdee_body_supermini_…` (each zip holds `merged-binary.bin`, flash at 0x0).
If the run is not there: repo → Actions → *Build M'DEE robot* → **Run workflow** (green button).

## After you change something
```
cd ~/Bot2-main
bash termux_push.sh "what I changed"
```

## If something goes wrong
| Message | Fix |
|---|---|
| `refusing to allow a Personal Access Token to create or update workflow` | Token lacks the **workflow** scope. Make a new token with `repo` + `workflow`, then `git credential reject` or delete `~/.git-credentials`, and push again. |
| `Authentication failed` | You typed your password. Use the token as password. |
| `Updates were rejected` (repo not empty) | `git pull origin main --allow-unrelated-histories --no-edit` then push again, or delete the GitHub repo and create a new empty one. |
| No **Actions** tab / no runs | Repo → Settings → Actions → General → *Allow all actions*. Branch must be `main` (the script sets it). |
| Still no `.github` on GitHub | Add the file by hand: GitHub app/browser → repo → *Add file* → *Create new file* → name `.github/workflows/mdee.yml` → paste the text of `workflow_backup/mdee.yml` (`cat workflow_backup/mdee.yml` in Termux, long-press to copy) → Commit. |
| Build red | Open the red step, copy the first lines of the error and send them to me. |
