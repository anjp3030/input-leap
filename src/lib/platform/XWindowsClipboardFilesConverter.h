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

#include "platform/XWindowsClipboard.h"

namespace inputleap {

//! Convert to/from copied files
/*!
Supports the \c text/uri-list target used by most file managers and the
\c x-special/gnome-copied-files target used by GNOME/GTK file managers.
Copied files are read into the clipboard data; pasted files are extracted
into a private temporary directory and offered as file URIs.
*/
class XWindowsClipboardFilesConverter : public IXWindowsClipboardConverter {
public:
    enum class Style { UriList, GnomeCopiedFiles };

    XWindowsClipboardFilesConverter(Display* display, const char* name, Style style);
    ~XWindowsClipboardFilesConverter() override;

    // IXWindowsClipboardConverter overrides
    IClipboard::EFormat getFormat() const override;
    Atom getAtom() const override;
    int getDataSize() const override;
    std::string fromIClipboard(const std::string&) const override;
    std::string toIClipboard(const std::string&) const override;

private:
    Atom m_atom;
    Style m_style;
};

} // namespace inputleap
