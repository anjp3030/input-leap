/*
 * InputLeap -- mouse and keyboard sharing utility
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

namespace inputleap {

//! Convert to/from copied files (CF_HDROP)
/*!
Files copied in Explorer are read into the clipboard data.  Files received
from another computer are extracted into a temporary directory and put on
the clipboard as CF_HDROP so that they can be pasted in Explorer.
*/
class MSWindowsClipboardFilesConverter : public IMSWindowsClipboardConverter {
public:
    MSWindowsClipboardFilesConverter();
    ~MSWindowsClipboardFilesConverter() override;

    // IMSWindowsClipboardConverter overrides
    IClipboard::EFormat getFormat() const override;
    UINT getWin32Format() const override;
    HANDLE fromIClipboard(const std::string&) const override;
    std::string toIClipboard(HANDLE) const override;
};

} // namespace inputleap
