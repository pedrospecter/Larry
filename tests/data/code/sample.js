// A small JavaScript file for the test of larry code.
import fs from "fs";
const path = require("path");

function readFile(name) {
  return fs.readFileSync(path.join(".", name));
}

const countWords = (text) => {
  return text.split(" ").length;
};

class Reader {
  read(name) {
    return countWords(readFile(name));
  }
}
