#include "capital.h"
#include <vector>
#include <yaml-cpp/yaml.h>
#include <curl_easy.h>
#include <curl_header.h>
#include <iostream>
#include <deque>

capital::capital(
  std::string configFile,
  std::mt19937& rng) :
    rng_(rng)
{
  // Load the config file.
  YAML::Node config = YAML::LoadFile(configFile);

  // Set up the Twitter client.
  twitter::auth auth;
  auth.setConsumerKey(config["consumer_key"].as<std::string>());
  auth.setConsumerSecret(config["consumer_secret"].as<std::string>());
  auth.setAccessKey(config["access_key"].as<std::string>());
  auth.setAccessSecret(config["access_secret"].as<std::string>());

  client_ = std::unique_ptr<twitter::client>(new twitter::client(auth));

  // Set up the verbly database.
  database_ = std::unique_ptr<verbly::database>(
    new verbly::database(config["verbly_datafile"].as<std::string>()));
}

void capital::run() const
{
  for (;;)
  {
    std::cout << "Generating tweet..." << std::endl;

    try
    {
      // Find a noun to use as the pictured item.
      std::cout << "Choosing pictured noun..." << std::endl;

      verbly::word pictured = getPicturedNoun();

      std::cout << "Noun: " << pictured.getBaseForm().getText() << std::endl;

      // Choose a picture of that noun.
      std::cout << "Finding an image..." << std::endl;

      Magick::Image image = getImageForNoun(pictured);

      // Generate the tweet text.
      std::cout << "Generating text..." << std::endl;

      std::string text = generateTweetText(pictured);

      std::cout << "Tweet text: " << text << std::endl;

      // Send the tweet.
      std::cout << "Sending tweet..." << std::endl;

      sendTweet(std::move(text), std::move(image));

      std::cout << "Tweeted!" << std::endl;

      // Wait.
      std::this_thread::sleep_for(std::chrono::hours(1));
    } catch (const could_not_get_image& ex)
    {
      std::cout << ex.what() << std::endl;
    } catch (const Magick::ErrorImage& ex)
    {
      std::cout << "Image error: " << ex.what() << std::endl;
    } catch (const Magick::ErrorCorruptImage& ex)
    {
      std::cout << "Corrupt image: " << ex.what() << std::endl;
    } catch (const twitter::twitter_error& ex)
    {
      std::cout << "Twitter error: " << ex.what() << std::endl;

      std::this_thread::sleep_for(std::chrono::hours(1));
    }

    std::cout << std::endl;
  }
}

verbly::word capital::getPicturedNoun() const
{
  verbly::filter whitelist = (verbly::notion::wnid == 100021939); // Artifacts

  verbly::filter blacklist =
    (verbly::notion::wnid == 106883725) // swastika
    || (verbly::notion::wnid == 104416901) // tetraskele
    || (verbly::notion::wnid == 103575691) // instrument of execution
    || (verbly::notion::wnid == 103829563) // noose
      ;

  verbly::query<verbly::word> pictureQuery = database_->words(
    (verbly::notion::fullHypernyms %= whitelist)
    && !(verbly::notion::fullHypernyms %= blacklist)
    && (verbly::notion::partOfSpeech == verbly::part_of_speech::noun)
    && (verbly::notion::numOfImages >= 1)
    // Blacklist ethnic slurs
    && !(verbly::word::usageDomains %= (verbly::notion::wnid == 106718862))
  );

  verbly::word pictured = pictureQuery.first();

  return pictured;
}

Magick::Image capital::getImageForNoun(verbly::word pictured) const
{
  // Accept string from Google Chrome
  std::string accept = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8";
  curl::curl_header headers;
  headers.add(accept);

  int backoff = 0;

  std::cout << "Getting URLs..." << std::endl;

  std::string lstdata;
  while (lstdata.empty())
  {
    std::ostringstream lstbuf;
    curl::curl_ios<std::ostringstream> lstios(lstbuf);
    curl::curl_easy lsthandle(lstios);
    std::string lsturl = pictured.getNotion().getImageNetUrl();
    lsthandle.add<CURLOPT_URL>(lsturl.c_str());

    try
    {
      lsthandle.perform();
    } catch (const curl::curl_easy_exception& e)
    {
      e.print_traceback();

      backoff++;
      std::cout << "Waiting for " << backoff << " seconds..." << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(backoff));

      continue;
    }

    backoff = 0;

    if (lsthandle.get_info<CURLINFO_RESPONSE_CODE>().get() != 200)
    {
      throw could_not_get_image();
    }

    std::cout << "Got URLs." << std::endl;
    lstdata = lstbuf.str();
  }

  std::vector<std::string> lstvec =
    verbly::split<std::vector<std::string>>(lstdata, "\r\n");
  if (lstvec.empty())
  {
    throw could_not_get_image();
  }

  std::shuffle(std::begin(lstvec), std::end(lstvec), rng_);

  std::deque<std::string> urls;
  for (std::string& url : lstvec)
  {
    urls.push_back(url);
  }

  bool found = false;
  Magick::Blob img;
  Magick::Image pic;

  while (!found && !urls.empty())
  {
    std::string url = urls.front();
    urls.pop_front();

    std::ostringstream imgbuf;
    curl::curl_ios<std::ostringstream> imgios(imgbuf);
    curl::curl_easy imghandle(imgios);

    imghandle.add<CURLOPT_HTTPHEADER>(headers.get());
    imghandle.add<CURLOPT_URL>(url.c_str());
    imghandle.add<CURLOPT_CONNECTTIMEOUT>(30);

    try
    {
      imghandle.perform();
    } catch (curl::curl_easy_exception error) {
      error.print_traceback();

      continue;
    }

    if (imghandle.get_info<CURLINFO_RESPONSE_CODE>().get() != 200)
    {
      continue;
    }

    std::string content_type =
      imghandle.get_info<CURLINFO_CONTENT_TYPE>().get();
    if (content_type.substr(0, 6) != "image/")
    {
      continue;
    }

    std::string imgstr = imgbuf.str();
    img = Magick::Blob(imgstr.c_str(), imgstr.length());

    try
    {
      pic.read(img);

      if (pic.rows() > 0)
      {
        std::cout << url << std::endl;
        found = true;
      }
    } catch (const Magick::ErrorOption& e)
    {
      // Occurs when the the data downloaded from the server is malformed
      std::cout << "Magick: " << e.what() << std::endl;
    }
  }

  if (!found)
  {
    throw could_not_get_image();
  }

  return pic;
}

std::string capital::generateTweetText(verbly::word pictured) const
{
  int msd = std::uniform_int_distribution<int>(1, 9)(rng_);
  int mag = std::uniform_int_distribution<int>(2, 9)(rng_);

  std::string money;
  for (int i=0; i<mag; i++)
  {
    money.insert(0, "0");

    if ((i % 3 == 2) && (mag > 3))
    {
      money.insert(0, ",");
    }
  }

  money.insert(0, std::to_string(msd));
  money.insert(0, "$");

  verbly::token nounTok = verbly::token::capitalize(
    verbly::token::casing::title_case,
    pictured);

  int aci = std::uniform_int_distribution<int>(0, 7)(rng_);
  verbly::token action;

  switch (aci)
  {
    case 0: action = { "No One Will Buy This", money, nounTok }; break;
    case 1: action = { "This", nounTok, "Is Not Worth", money }; break;
    case 2: action = { "We Can't Get Rid Of This", money, nounTok }; break;
    case 3: action = { "Millenials Will No Longer Spend", money, "For",
      verbly::token::indefiniteArticle(nounTok) }; break;
    case 4: action = { "Why Does This", money, nounTok, "Exist?" }; break;
    case 5: action = { "Someone Spent", money, "Making This", nounTok,
      "That No One Will Buy" }; break;
    case 6: action = { "What A Waste: This", nounTok, "Will Rot Because",
      "No One Can Afford Its", money, "Price Tag" }; break;
    case 7: action = { "This", money, nounTok, "Was A Mistake" }; break;
  }

  return action.compile();
}

void capital::sendTweet(std::string text, Magick::Image image) const
{
  Magick::Blob outputBlob;
  image.magick("jpg");
  image.write(&outputBlob);

  long media_id = client_->uploadMedia("image/jpeg",
    (const char*) outputBlob.data(), outputBlob.length());

  client_->updateStatus(std::move(text), {media_id});
}
