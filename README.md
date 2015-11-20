# Git-ptp

This project is forked from git

This git is modifed for Eclipse PTP synchronized prjects.

The mainly change are below:
 - change git ignore file to ".ptp-gitignore"
 - treat all submodules as part of the git repo
 - ignore all files in .git and the .git directory in submodules
 - change default git config file to "~/ptp-bin/.ptp-gitconfig"


### How to use
Put the source code to remote server
```sh
autoreconf -i
./configure
make git
cp git ~/ptp-bin/
```
Open Eclipse, goto Window -> Preferences -> Remote Development -> Synchronized Projects -> Git Binary Locations

Select the git in ~/ptp-bin/



### Troubleshooting
If it's failed when create synchronized project, please try this:
```sh
cp ptp-templates ~/ptp-bin/
~/ptp-bin/ptp-git config --global init.templateDir "/home/$USER/ptp-bin/ptp-templates/"
```