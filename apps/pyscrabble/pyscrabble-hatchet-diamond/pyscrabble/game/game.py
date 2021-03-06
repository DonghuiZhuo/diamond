from pyscrabble.game.pieces import Bag, Move, Letter
from pyscrabble.game.player import Player,PlayerInfo
from pyscrabble.exceptions import GameOverException, BagEmptyException
from pyscrabble.constants import *
from pyscrabble.lookup import *
from random import shuffle

import sys
sys.path.append("../../../platform/build/bindings/python/")
sys.path.append("../../../platform/bindings/python/")
from libpydiamond import *

class ScrabbleGame:
    '''
    ScrabbleGame class.  Keeps track of the players in the game, the letters and the moves played.
    '''
    
    def __init__(self, name = '', options=None):
        '''
        Initialize a new game.
        
        @param name: Game ID
        '''

        self.name = name
        self.players = DStringList()
        DStringList.Map(self.players, "game:" + name + ":players")
        self.started = DBoolean()
        DBoolean.Map(self.started, "game:" + name + ":started")
        self.complete = DBoolean()
        DBoolean.Map(self.complete, "game:" + name + ":complete")
        self.moves = []
        self.words = []
        self.usedModifiers = []
        self.passedMoves = DLong()
        DLong.Map(self.passedMoves, "game:" + name + ":passedmoves")
        self.currentPlayer = DString()
        DString.Map(self.currentPlayer, "game:" + name + ":currentplayer")
        self.log = []
        self.bag = Bag( self.name, rules='en' )
        self.creator = DString()
        DString.Map(self.creator, "game:" + name + ":creator")
        self.timer = None
        self.spectatorsAllowed = True
        
        self.onboardLetters = DStringList()
        DStringList.Map(self.onboardLetters, "game:" + name + ":onboardletters")
        self.onboardLetterScores = DList()
        DList.Map(self.onboardLetterScores, "game:" + name + ":onboardletterscores")
        self.onboardX = DList()
        DList.Map(self.onboardX, "game:" + name + ":onboardx")
        self.onboardY = DList()
        DList.Map(self.onboardY, "game:" + name + ":onboardy")
    
    def reset(self):
        for player in self.getPlayers():
            player.reset()
        
        self.setCreator("")
        self.currentPlayer.Set("")
        self.started.Set(False)
        self.complete.Set(False)
        self.players.Clear()
        self.bag.reset(rules='en')
        self.passedMoves.Set(0)
        
        self.clearOnboardMove()
        
        empty = DBoolean()
        DBoolean.Map(empty, "game:" + self.name + ":board:empty")
        empty.Set(True)
        
        for x in range(0, 15):
            for y in range(0, 15):
                letterPresent = DBoolean()
                DBoolean.Map(letterPresent, "game:" + self.name + ":tile:" + repr(y * BOARD_WIDTH + x) + ":letterPresent")
                letterPresent.Set(False)
    
    def setCreator(self, playerName):
        self.creator.Set(playerName)
    
    def getCreator(self):
        return self.creator.Value()
    
    def getOnboardMove(self):
        result = Move()
        for i in range(0, self.onboardLetters.Size()):
            result.addMove(Letter(self.onboardLetters.Value(i), self.onboardLetterScores.Value(i)), self.onboardX.Value(i), self.onboardY.Value(i))
        return result
    
    def clearOnboardMove(self):
        self.onboardLetters.Clear()
        self.onboardLetterScores.Clear()
        self.onboardX.Clear()
        self.onboardY.Clear()
        
    def removeFromOnboard(self, letter, x, y):
        index = -1
        for i in range(0, self.onboardLetters.Size()):
            if self.onboardLetters.Value(i) == letter.getLetter() and self.onboardX.Value(i) == x and self.onboardY.Value(i) == y:
                index = i
        if index != -1:
            self.onboardLetters.Erase(index)
            self.onboardLetterScores.Erase(index)
            self.onboardX.Erase(index)
            self.onboardY.Erase(index)
        
    def addToOnboard(self, letter, x, y):
        self.onboardLetters.Append(letter.getLetter())
        self.onboardLetterScores.Append(letter.getScore())
        self.onboardX.Append(x)
        self.onboardY.Append(y)
    
    def getName(self):
        return self.name
    
    def getStatus(self):
        if (self.isStarted()):
            status = ServerMessage([STATUS_STARTED])
        else:
            status = ServerMessage([NOT_STARTED])
        if (self.isComplete()):
            status = ServerMessage([COMPLETE])
        return status
    
    def getDistribution(self):
        '''
        Get Letter distribution
        
        @return: dict(Letter,Count)
        '''
        
        return self.bag.getDistribution()
    
    def isBagEmpty(self):
        '''
        Check if the letter bag is empty
        
        @return: True if the Bag is empty
        '''
        return self.bag.isEmpty()
        
    
    def addUsedModifier(self, pos):
        '''
        Add a used modifier to the list.
        
        These are word or letter modifiers that we have already used.
        
        @param pos: (x,y) position
        '''
        self.usedModifiers.append(pos)
    
    def hasUsedModifier(self, pos):
        '''
        Check to see if we've used a modifier
        
        @param pos: (x,y) position
        '''
        
        return pos in self.usedModifiers;
        
    
    def getLetters(self, numLetters):
        '''
        Get letters from the bag
        
        @param numLetters: Number of letters to get
        @return: List of Letters or [] if there or no letters left
        @see: L{pyscrabble.game.pieces.Letter}
        '''
        
        try:
            return self.bag.getLetters( numLetters )
        except BagEmptyException: pass
        
        return []
    
    def addPlayer(self, player):
        '''
        Add a player to the game
        
        @param player: Player to add to the game
        @see: L{pyscrabble.game.player.Player}
        '''
        
        self.players.Append( player.getUsername() )
    
    def getGameId(self):
        '''
        Get the Game ID
        
        @return: Game ID of this game
        '''
        
        return self.name
    
    def getNumberOfPlayers(self):
        '''
        Get number of players in the game
        
        @return: Number of players in the game
        '''
        
        return self.players.Size()
    
    def isStarted(self):
        '''
        Check if the game is started.
        
        @return: True if the game is started and unpaused.
        '''
        
        return self.started.Value()
    
    def isComplete(self):
        '''
        Check if the game is complete.  The game is complete when all players cannot make a move and have to pass.
        
        @return: True if the game is complete.  
        '''
        
        return self.complete.Value()
    
    def start(self):
        '''
        Start the game.
        '''
        
        self.started.Set(True)
        
        indices = range(0, self.players.Size())
        shuffle(indices)
        tempPlayers = []
        for playerName in self.players.Members():
            tempPlayers.append(playerName)
        self.players.Clear()
        for index in indices:
            self.players.Append(tempPlayers[index])
        
            
    def getCurrentPlayer(self):
        '''
        Get current player
        
        @return: Player who has control of the board.
        @see: L{pyscrabble.game.player.Player}
        '''
        return Player(self.currentPlayer.Value())
        
    
    def getNextPlayer(self):
        '''
        Get the next player who has control of the board.
        
        @return: Next Player who has control of the board.
        @see: L{pyscrabble.game.player.Player}
        '''
                
        if (self.players.Size() == 0):
            return None
        
        self.currentPlayer.Set(self.players.Value(0))
        self.players.Erase(0)
        self.players.Append(self.currentPlayer.Value())
        return Player(self.currentPlayer.Value())
    
    def getPlayers(self):
        '''
        Return a copy of the list of Players
        
        @return: List of Players
        @see: L{pyscrabble.game.player.Player}
        '''
        
        #return self.players.Members()
        result = []
        for playerName in self.players.Members():
            result.append(Player(playerName, self.name))
        return result
    
    def hasPlayer(self, player):
        '''
        Check to see if a player is in the game
        
        @param player: Player
        @return: True if player is in the game
        @see: L{pyscrabble.game.player.Player}
        '''
        
        return player.getUsername() in self.players.Members()
    
    def playerLeave(self, player):
        '''
        Remove a player from the game and return his/her Letters to the bag
        
        @param player: Player to remove
        @see: L{pyscrabble.game.player.Player}
        @see: L{pyscrabble.game.pieces.Letter}
        '''
        
        self.players.Remove(player.getUsername())
        self.returnLetters( player.getLetters() )
        if self.getNumberOfPlayers() == 0:
            self.reset()
            globalGamesSet = DStringSet()
            DStringSet.Map(globalGamesSet, "global:games")
            globalGamesSet.Remove(self.name)
    
    def addMoves(self, moves, player):
        '''
        Add a move to the board
        
        @param moves: Moves to add to the board
        @param player: Player who added the move
        @see: L{pyscrabble.game.pieces.Move}
        '''
        
        self.passedMoves.Set(0) # Rest pass counter
        #TODO
        score = 0
        for move in moves:
            word = move.getWord()
            score = score + move.getScore()
            self.words.append( word )
            self.moves.append( move )
        
    
    
    def getMoves(self):
        '''
        Return List of moves on the board
        
        @return: List of moves on the board
        @see: L{pyscrabble.game.pieces.Move}
        '''
        
        return self.moves
    
    def hasWord(self, word):
        '''
        Check to see if a word is on the board
        
        @param word: Word to check
        @return: True if the word is on the board.
        '''
        
        return word in self.words
    
    def resetPassCount(self):
        '''
        Reset the pass counter
        '''
        self.passedMoves.Set(0)
        
    
    def passMove(self):
        '''
        Pass a move.
        
        If the number of passed moves is equal to the number of players, all players have passed their moves
        and the game is over.
        
        @raise GameOverException: If all players have passed their moves and the game is over.
        '''
        
        self.passedMoves.Set(self.passedMoves.Value() + 1)
        if (self.passedMoves.Value() == self.players.Size()):
            raise GameOverException
    
    def getWinners(self, exclude=None):
        '''
        Return the player(s) with the highest score
        
        @param exclude: Player to exclude from winning (say, if they timed out)
        @return: List of players with the highest score.
        @see: L{pyscrabble.game.player.Player}
        '''
        
        tmp = self.players[:]
        
        if exclude is not None:
            tmp.remove(exclude)
        
        maxScore = None
        for player in tmp:
            maxScore = max(maxScore, player.getScore())
        
        winners = []
        for player in tmp:
            if player.getScore() == maxScore:
                winners.append( player )
        return winners
    
    def returnLetters(self, letters):
        '''
        Return letters to the Bag.
        
        @param letters: Letters to put back in the back
        @see: L{pyscrabble.game.pieces.Letter}
        @see: L{pyscrabble.game.pieces.Bag}
        '''
        
        self.bag.returnLetters(letters)
    
    def isInProgress(self):
        '''
        Check whether the game is started.  The game is considered in progress even if it is paused.
        
        @return: True if the game has been started.
        '''
        
        return self.started
    
    def getPlayer(self, player):
        '''
        Get a player out of the game
        
        @param player: Player
        @return: Player in the game, or None if player is not in the game.
        @see: L{pyscrabble.game.player.Player}
        '''
        
        for _playerName in self.players.Members():
            if player.getUsername() == _playerName:
                return Player(_playerName, self.name)
        return None
    
    def isCurrentPlayer(self, player):
        '''
        Check to see if C{player} is the current player
        
        @param player: Player to checl
        @return: True if C{player} is the current player.
        '''
        return self.currentPlayer.Value() == player.getUsername()
    
    def setComplete(self):
        '''
        Mark this game as complete
        '''
        self.complete.Set(True)
    
    def appendLogMessage(self, log):
        '''
        Add logging information
        
        Log is a tuple: (level, message)
        
        @param log: Log Message tuple
        '''
        self.log.append(log)
    
    def getLog(self):
        '''
        Retrieve log
        
        Log is a list of tuples: (level, message)
        
        @return: Game Log
        '''
        return self.log[:]
        
    
    def getCountLetters(self):
        '''
        Return the number of letters left in the bag
        
        @return: Count of letters left in bag
        '''
        return self.bag.getCount()
        
        
class ScrabbleGameInfo(object):
    '''
    Information about a Scrabble game.
    
    Contains the name, status and number of players.
    '''
    
    def __init__(self, game=None):
        '''
        Initialize a ScrabbleGameInfo object from a ScrabbleGame
        
        @param game: ScrabbleGame
        @see: L{ScrabbleGame}
        '''
        
        if game:
            self.players = [ PlayerInfo(p.getUsername(), p.getScore(), len(p.getLetters()), p.time) for p in game.getPlayers() ]
            self.numPlayers = len(self.players)
            self.name = game.getGameId()
            
            if (game.isStarted()):
                self.status = ServerMessage([STATUS_STARTED])
            else:
                self.status = ServerMessage([NOT_STARTED])
            
            if (game.isComplete()):
                self.status = ServerMessage([COMPLETE])
            
            game = None
    
    def getName(self):
        '''
        Get the Game ID
        
        @return: Game ID
        '''
        
        return self.name
    
    def getNumberOfPlayers(self):
        '''
        Get number of players
        
        @return: Number of players
        '''
        
        return self.numPlayers
    
    def getPlayers(self):
        '''
        Get players
        
        @return: List of players
        '''
        s = unicode('')
        for _p in self.players:
            s = s + repr(_p)+ unicode(' ')
        return s
        
    
    def getStatus(self):
        '''
        Get status
        
        @return: Get status of game
        '''
        
        return self.status
    
    def __repr__(self):
        '''
        Print Game ID
        
        @return: Game ID
        '''
        
        return self.name
        
