import unittest

from ivxedit.binfmt import align16, pack_words, u16, u32, words


class BinfmtTest(unittest.TestCase):
    def test_align16(self):
        self.assertEqual([align16(n) for n in (0, 1, 15, 16, 17)], [0, 16, 16, 16, 32])

    def test_words_are_big_endian(self):
        b = pack_words([0x0102, 0xFFFE])
        self.assertEqual(b, b'\x01\x02\xff\xfe')
        self.assertEqual(words(b, 0, 2), [0x0102, 0xFFFE])
        self.assertEqual(u16(b, 2), 0xFFFE)
        self.assertEqual(u32(b, 0), 0x0102FFFE)


if __name__ == '__main__':
    unittest.main()
