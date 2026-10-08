// A small C# file for the test of larry code.
using System;
using System.IO;

namespace Sample
{
    public class Reader
    {
        public string Read(string path)
        {
            return File.ReadAllText(path);
        }

        public int CountWords(string text)
        {
            return text.Split(' ').Length;
        }
    }
}
