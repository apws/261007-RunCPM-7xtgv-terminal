"""Explicit, rerunnable USB serial-group setup; never open or reset a port."""
import glob
import grp
import os
import pwd
import stat
import subprocess
import sys


def serial_groups(paths):
    groups = set()
    for path in paths:
        device = os.stat(path)
        if not stat.S_ISCHR(device.st_mode):
            continue
        group = grp.getgrgid(device.st_gid).gr_name
        # Never offer membership in arbitrary privileged device groups.
        if group in ('uucp', 'dialout') and device.st_mode & stat.S_IWGRP:
            groups.add(group)
        else:
            print(f'{path}: group {group} needs administrator review; skipped.')
    return sorted(groups)


def main():
    if os.geteuid() == 0:
        raise SystemExit('Run setup as your normal user, not through sudo.')
    paths = sorted(glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*'))
    if not paths:
        print('No USB serial device found. Connect it and rerun setup.sh --serial-only.')
        return
    account = pwd.getpwuid(os.getuid())
    configured = os.getgrouplist(account.pw_name, account.pw_gid)
    for group in serial_groups(paths):
        gid = grp.getgrnam(group).gr_gid
        if gid in configured:
            print(f'{account.pw_name} already belongs to {group}; no change needed.')
        else:
            print(f'USB serial group: {group}. Membership grants access to its devices.')
            question = f'Add {account.pw_name} to {group} using sudo? [y/N] '
            if sys.stdout.isatty() and os.environ.get('TERM') != 'dumb' \
                    and 'NO_COLOR' not in os.environ:
                question = '\033[31m' + question + '\033[0m'
            answer = input(question)
            if answer.lower() != 'y':
                print('Serial-group membership unchanged.')
                continue
            subprocess.run(['sudo', 'usermod', '-aG', group, account.pw_name], check=True)
            print(f'Added {account.pw_name} to {group}.')
        if gid not in os.getgroups() and gid != os.getgid():
            print('Log out fully and back in to activate access for all apps. '
                  'Reflash can also enter an already configured group via newgrp.')


if __name__ == '__main__':
    main()
