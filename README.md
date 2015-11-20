This project is forked from git

This git is modifed for Eclipse PTP synchronized prjects.


The mainly change are below:
1. change git ignore file to ".ptp-gitignore"
2. treat all submodules as part of the git repo
3. ignore all files in .git and the .git directory in submodules
4. change default git config file to "~/ptp-bin/.ptp-gitconfig"


How to use:
Put the source code to remote server

autoreconf -i
./configure
make git
cp git ~/ptp-bin/

Open Eclipse, goto Window -> Preferences -> Remote Development -> Synchronized Projects -> Git Binary Locations
Select the git in ~/ptp-bin/



Troubleshooting:
If it's failed when create synchronized project, please try this:

cp ptp-templates ~/ptp-bin/
~/ptp-bin/ptp-git config --global init.templateDir "/home/$USER/ptp-bin/ptp-templates/"
