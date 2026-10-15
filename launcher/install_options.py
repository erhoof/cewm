from __future__ import annotations

from dataclasses import dataclass
from typing import List

from upgrade_process import (DESKTOP_ICON_FLAG, FRESH_INSTALL_FLAG,
                             NO_LAUNCH_FLAG, START_MENU_FLAG)


@dataclass(frozen=True)
class InstallOptions:
    fresh: bool = False
    desktop_icon: bool = False
    start_menu: bool = False
    launch_after: bool = True

    @property
    def verb(self) -> str:
        return "Installing" if self.fresh else "Upgrading"

    def to_arguments(self) -> List[str]:
        args: List[str] = []
        if self.fresh:
            args.append(FRESH_INSTALL_FLAG)
        if self.desktop_icon:
            args.append(DESKTOP_ICON_FLAG)
        if self.start_menu:
            args.append(START_MENU_FLAG)
        if not self.launch_after:
            args.append(NO_LAUNCH_FLAG)
        return args


def parse_options(argv: List[str]) -> InstallOptions:
    return InstallOptions(fresh=FRESH_INSTALL_FLAG in argv,
                          desktop_icon=DESKTOP_ICON_FLAG in argv,
                          start_menu=START_MENU_FLAG in argv,
                          launch_after=NO_LAUNCH_FLAG not in argv)
