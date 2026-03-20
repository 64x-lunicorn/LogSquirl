/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "version.h"

#include "logsquirl_version.h"

QLatin1String logsquirlVersion()
{
    return QLatin1String( LOGSQUIRL_VERSION );
}

QLatin1String logsquirlBuildDate()
{
    return QLatin1String( LOGSQUIRL_DATE );
}

QLatin1String logsquirlCommit()
{
    return QLatin1String( LOGSQUIRL_COMMIT );
}

QLatin1String logsquirlGitVersion()
{
    return QLatin1String( LOGSQUIRL_GIT_VERSION );
}
