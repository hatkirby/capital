#ifndef CAPITAL_H_09912EAE
#define CAPITAL_H_09912EAE

#include <random>
#include <string>
#include <verbly.h>
#include <stdexcept>
#include <memory>
#include <Magick++.h>
#include <twitter.h>

class capital {
public:

  capital(
    std::string configFile,
    std::mt19937& rng);

  void run() const;

private:

  verbly::word getPicturedNoun() const;

  Magick::Image getImageForNoun(verbly::word pictured) const;

  std::string generateTweetText(verbly::word pictured) const;

  void sendTweet(std::string text, Magick::Image image) const;

  class could_not_get_image : public std::runtime_error {
  public:

    could_not_get_image() : std::runtime_error("Could not get image for noun")
    {
    }
  };

  std::mt19937& rng_;
  std::unique_ptr<verbly::database> database_;
  std::unique_ptr<twitter::client> client_;

};

#endif /* end of include guard: CAPITAL_H_09912EAE */
