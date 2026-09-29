#pragma once

#include <QString>
#include <filesystem>

namespace virgil::paths {

std::filesystem::path native(const QString& path);
QString fromNative(const std::filesystem::path& path);
QString rootDirectory(const QString& path);
void validateName(const QString& name);
void validateRelative(const QString& path, bool allowMetadata = false);
// Allows a missing final component, rejects symlinks in every existing component.
QString inside(const QString& root, const QString& relative, bool allowMetadata = false);
QString relative(const QString& root, const QString& absolute);
bool isWithin(const QString& root, const QString& absolute);
bool nameExists(const QString& directory, const QString& name);
QString identity(const QString& path);
void renameWithoutOverwrite(const QString& source, const QString& destination);
void finalizeWithoutOverwrite(const QString& staged, const QString& destination);

} // namespace virgil::paths
