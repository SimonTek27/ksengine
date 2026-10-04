$path = 'E:\Users\Simon\source\repos\kseditor\src\engine\KsTypes.h'
$sw = [System.IO.StreamWriter]::new($path, $false)
function W($s) { $sw.WriteLine($s) }

# Chunk 1: pragma once through type aliases
W('#pragma once')
W('')
W('#ifdef KSENGINE_NO_QT')
W('')
W('using uint = unsigned int;')
W('using ulong = unsigned long;')
W('using ushort = unsigned short;')
W('using uchar = unsigned char;')
W('using qint8 = std::int8_t;')
W('using quint8 = std::uint8_t;')
W('using qint16 = std::int16_t;')
W('using quint16 = std::uint16_t;')
W('using qint32 = std::int32_t;')
W('using quint32 = std::uint32_t;')
W('using qint64 = std::int64_t;')
W('using quint64 = std::uint64_t;')
W('using qreal = double;')
W('')
W('#endif // KSENGINE_NO_QT')

$sw.Flush()
$sw.Close()