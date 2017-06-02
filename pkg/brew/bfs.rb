class Bfs < Formula
  version '1.0.1'
  desc "Breadth-first version of find."
  homepage "https://github.com/tavianator/bfs"
  url "https://github.com/tavianator/bfs/archive/#{version}.tar.gz"
  sha256 "e4a1fd0bfc364960f87f67462fd48c9e1d09e43e9e8372ada774ee713d37a3e1"

  depends_on "make" => :build

  def install
    system "make", "release"
    bin.install "bfs"
  end

  test do
    system bin/"bfs", "."
  end
end
