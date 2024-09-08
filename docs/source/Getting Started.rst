.. _Getting Started:

Getting Started
===============

This may seem somewhat complicated, but it's much easier than it may feel like, so let's dive in!

Installation Of The App
-----------------------

.. note::
 lmao fill it in once the app is packaged in an installation file

The installer will be available under the releases tab in GitHub.


File Structure
--------------

The file structure is not too complicated, and only an overview is required for functioning.
Inside the main application folder, there is a folder called *Data*. This folder contains the database, the billed rolls, the autosaved rolls which are in the middle of being billed in JSON format,
and the music used in the :ref:`DJ` window.

.. note::
   Billed rolls will have the same name as the loaded rolls, and will contain a specific number of images named in a specific manner in order for printing and delivering
   of the snaps to occur.

Loading The Mess List
---------------------

.. note::
   This needs to be done only when you have downloaded the app for the first time or when the mess list has changed.

The mess list is a csv file which consists of the ID Numbers, Names, Genders and Hostels of all the students in the college. You can keep it anywhere on your
PC. To load the mess list, drag the :ref:`Logger` window away to reveal the toolbar located on the top of the main app window. Simply click Tools -> Load Mess List.
Any csv file will appear green. Simply select it. Upon succesful loading, a message will pop up, and za list will be loaded.

.. note::
   Brackets are banned in the names of the mess list, and some names do contain them. In such cases, editing the mess list might be required.
   It will entail opening the mess list, scrolling to the infamous person, and removing the brackets.
   For example, skibidi (toilet) fanum will have to be changed to either skibidi toilet faunum or simply skibidi fanum.

.. image:: MessList.PNG
  :width: 800


Loading The Roll
----------------

The roll is a folder containing all the images in one *DoPy Roll*. It will have a maximum of 40 images (tradition), and will have a suffix of *R*. Load the roll similarly
to the mess list (Tools->Load Roll). Choose the roll, and just click ok.


Billing Window
--------------

.. image :: Billing.webp
   :width: 800

Using this window, you can search for names, IDs, hostels, etc. and bill them accordingly. The billing window comes with a parser built into so you can querry the database of students with more control.

The process for billing is fairly simple. A "roll window" opens up along with the billing window, and displays the images.
Using the detailed roll books, one can simply type the ID number of a person, click the same "ID button" from the list that pops up, and watch it get added in the "billed list".
After billing one image with all the ID numbers, clicking next displays the next image, while preserving the billed image's status.

After all the images have been billed, simply click export (located in the billing window, in the same horizontal position as the search bar), and the roll is billed!
The exported roll will be stored in the Data folder, inside the main application folder.


There is a syntax for searching in the billing window, and it utilizes full-text search (fts) to aid it. Link to the page -> :ref:`Search Syntax`


.. _Logger:

Logger
------

The Logger, as the name implies, logs stuff! Nothing escapes its sight. You can filter out logs by typing into the text box. GUI.Music will show you only music related log messages and so on.

.. image:: Logger.PNG


.. _DJ:

Music
-----

.. note::
   Although the internals for playing and visualising music are close to ready this feature is still unfinished. This will be implemented before BOSM.

In the same toolbar as the Tools option, the Music option exists. The DJ is spawned upon selection, and it has three options; start, change and stop.
Play the handpicked tunes lesgoo.

.. video:: DJ.webm
   :width: 600

Dinosaur
--------

.. image:: giraffe.jpg
  :width: 600
