#include "DestinNetworkAlt.h"

void DestinNetworkAlt::initTemperatures(int layers, uint * centroids){
    temperatures = new float[layers];

    for(int l = 0 ; l < layers ; l++){
        temperatures[l] = centroids[l];
    }

    if(layers >= 2){
        temperatures[layers - 1] *= 2;
    }
    if(layers >=3){
        temperatures[layers - 2] *= 2;
    }
}

float *** DestinNetworkAlt::getCentroidImages(){
    if(centroidImages==NULL){
        centroidImages = Cig_CreateCentroidImages(destin, centroidImageWeightParameter);
    }
    return centroidImages;
}


void DestinNetworkAlt::decodeLabelLayerCentroid(short label, int & layer_out, int & centroid_out){
   return; //TODO
}

int DestinNetworkAlt::decodeLabelChildNum(short label){
    return 0; //TODO
}


void DestinNetworkAlt::paintCentroidImage(int cent_layer, int centroid, int x, int y, cv::Mat & img){
    int w = Cig_GetCentroidImageWidth(destin, cent_layer); //TODO: what is smallest depth
    cv::Mat subimage(w, w, CV_32FC1, getCentroidImages()[cent_layer][centroid]); // wrap centroid image with cv::Mat
    cv::Rect roi(cv::Point( x, y ), subimage.size()); // make roi = region of interest
    cv::Mat dest = img( roi );     // get the subsection of img where the centroid image will go
    subimage.copyTo(dest);                                                  // copy the subimage to the subsection of img
    return;
}

void DestinNetworkAlt::calcChildCoords(int px, int py, int child_no, int child_layer, int & child_x_out, int & child_y_out){
    int w;
    switch (child_no) {
        case 0:
            child_x_out = px;
            child_y_out = py;
            break;
        case 1:
            w = Cig_GetCentroidImageWidth(getNetwork(), child_layer);
            child_x_out = px + w;
            child_y_out = py;
            break;
        case 2:
            w = Cig_GetCentroidImageWidth(getNetwork(), child_layer);
            child_x_out = px;
            child_y_out = py + w;
            break;
        case 3:
            w = Cig_GetCentroidImageWidth(getNetwork(), child_layer);
            child_x_out = px + w;
            child_y_out = py + w;
            break;
        default :
            throw std::domain_error("DestinNetworlAlt::calcNewCoords: invalid child number.");
    };
    return;
}

void DestinNetworkAlt::displayTreeHelper(std::vector<short> & tree, int tree_pos, int px, int py, cv::Mat & img){
    int label = tree.at(tree_pos);
    if(label != -1 ){
        int layer, cent;
        decodeLabelLayerCentroid(label, layer, cent);
        paintCentroidImage(layer, cent, px, py, img );
    }

    if(tree_pos == tree.size() - 1){
        return;
    }

    tree_pos++;
    int child_x, child_y, child_layer, dummy;
    int child_label = tree.at(tree_pos);
    decodeLabelLayerCentroid(child_label, child_layer, dummy);
    calcChildCoords(px, py, decodeLabelChildNum(child_label), child_layer, child_x, child_y );
    displayTreeHelper(tree, tree_pos, child_x, child_y, img );

}


DestinNetworkAlt::DestinNetworkAlt(SupportedImageWidths width, unsigned int layers,
        unsigned int centroid_counts [], bool isUniform ) :
        training(true),
        beta(.01),
        lambda(.1),
        gamma(.1),
        isUniform(isUniform),
        centroidImages(NULL),
        centroidImageWeightParameter(1.0)
        {

    uint input_dimensionality = 16;
    uint c, l;
    callback = NULL;
    initTemperatures(layers, centroid_counts);
    float starv_coef = 0.05;
    uint n_classes = 0;//doesn't look like its used
    uint num_movements = 0; //this class does not use movements

    //figure out how many layers are needed to support the given
    //image width.
    bool supported = false;
    for (c = 4, l = 1; c <= MAX_IMAGE_WIDTH ; c *= 2, l++) {
        if (c == width) {
            supported = true;
            break;
        }
    }
    if(!supported){
        throw std::logic_error("given image width is not supported.");
    }
    if (layers != l) {
        throw std::logic_error("Image width does not match the given number of layers.");
    }
    destin = InitDestin(
            input_dimensionality,
            layers,
            centroid_counts,
            n_classes,
            beta,
            lambda,
            gamma,
            temperatures,
            starv_coef,
            num_movements,
            isUniform
     );


    SetLearningStrat(destin, CLS_FIXED);
    ClearBeliefs(destin);
    destin->fixedLearnRate = 0.1;
    isTraining(true);
}

DestinNetworkAlt::~DestinNetworkAlt() {
    if(centroidImages != NULL){
        Cig_DestroyCentroidImages(destin,  centroidImages);
    }

    if(destin!=NULL){
        DestroyDestin(destin);
        destin = NULL;
    }
    if(temperatures!=NULL){
        delete [] temperatures;
        temperatures = NULL;
    }
}

void DestinNetworkAlt::setTemperatures(float temperatures[]){
    memcpy(this->temperatures, temperatures, sizeof(float) * getLayerCount());
    memcpy(destin->temp, temperatures, sizeof(float) * getLayerCount());
    for(int l = 0; l < getLayerCount(); l++){
        for(int n = 0 ; n < destin->layerSize[l]; n++){
            GetNodeFromDestinI(destin, l, n)->temp = temperatures[l];
        }
    }
}

void DestinNetworkAlt::doDestin( //run destin with the given input
        float * input_dev //pointer to input memory on device
        ) {
    FormulateBelief(destin, input_dev);

    if(this->callback != NULL){
        this->callback->callback(*this );
    }
}

void DestinNetworkAlt::isTraining(bool isTraining) {
    this->training = isTraining;
    for(int l = 0 ; l < destin->nLayers ; l++){
       destin->layerMask[l] = isTraining ? 1 : 0;
    }
}


/** print given node's centroid's locations
 *  The Centroids consists of 3 main parts, the dimensions which cluster
 *  on the nodes input, the dimensions which cluster on its previous belief
 *  and the dimensions which cluster on the parents previous belief.
 */
void DestinNetworkAlt::printNodeCentroidPositions(int layer, int row, int col){
    Node * n = getNode(layer, row, col);
    for(int centroid  = 0 ; centroid < n->nb ; centroid++){
        printf("centroid %i: input ", centroid);
        int dimension;
        for(dimension = 0 ; dimension < n->ni ; dimension++){
            if(dimension % (n->ni / 4) == 0){
                printf("\n");
            }
            printf("%.5f ", n->mu[centroid*n->ns + dimension]);
        }
        printf("\n");
        printf(" self ");
        for(int end = dimension + n->nb ; dimension < end ; dimension++){
            printf("%.5f ", n->mu[centroid*n->ns + dimension]);
        }
        printf(" parent ");
        for(int end = dimension + n->np ; dimension < end ; dimension++){
            printf("%.5f ", n->mu[centroid*n->ns + dimension]);
        }
        printf("\n");
    }
}

/** Prints a grid of winning centroid numbers for a layer.
  * The grid is the size of the layer in nodes. Each
  * node has a winning centroid number.
  */
void DestinNetworkAlt::printWinningCentroidGrid(int layer){
    uint width = (uint)sqrt(destin->layerSize[layer]);
    uint nidx = 0;
    Node * nodelist = GetNodeFromDestin(destin, layer, 0, 0);
    printf("Winning centroids grid - layer %i:\n", layer);
    for( int r  = 0 ; r < width ; r++){
        for(int c = 0 ; c < width ; c++ , nidx++){
            printf("%i ", nodelist[nidx].winner);
        }
        printf("\n");
    }
}

/** Shows an image representing winning centroids
  * Each pixel represents a node where the grayscale color is determined
  * by the node's winning_centroid_index / # of centroids.
  * @param layer = layer to show
  * @param zoom = scales the image by this factor
  */
void DestinNetworkAlt::imageWinningCentroidGrid(int layer, int zoom, const string& window_name){
    uint width = (uint)sqrt(destin->layerSize[layer]);

    //initialize or create new grid if needed
    if(winningGrid.rows != width || winningGrid.cols != width){
        winningGrid = cv::Mat(width, width, CV_32FC1);
    }
    int nb = destin->nb[layer];
    float * data = (float *)winningGrid.data;
    for(int n = 0 ; n < destin->layerSize[layer] ; n++){
        data[n]  = (float)GetNodeFromDestinI(destin, layer, n)->winner / (float) nb;
    }
    cv::resize(winningGrid,winningGridLarge, cv::Size(), zoom, zoom, cv::INTER_NEAREST);
    cv::imshow(window_name, winningGridLarge);
    return;
}

void DestinNetworkAlt::printNodeObservation(int layer, int row, int col){
    Node * n = getNode(layer, row, col);
    printf("Node Observation: layer %i, row %i, col: %i\n", layer, row, col);
    printf("input: ");
    int i = 0;
    for(int c = 0 ; c < n->ni ; c++ ){
        if(c % (n->ni / 4) == 0){
            printf("\n");
        }
        printf("%f ", n->observation[i]);
        i++;
    }
    printf("\nPrevious Belief: ");
    for(int c  = 0 ; c < n->nb ; c++ ){
        printf("%f ", n->observation[i]);
        i++;
    }
    printf("\nParent prev belief: ");
    for(int c  = 0 ; c < n->np ; c++ ){
        printf("%f ", n->observation[i]);
        i++;
    }
    printf("\n");
}

void DestinNetworkAlt::printNodeBeliefs(int layer, int row, int col){
    Node * n = getNode(layer, row, col);
    printf("Node beliefs: layer%i, row %i, col: %i\n", layer, row, col);
    for(int i = 0; i < n->nb ; i++){
        printf("%f ", n->pBelief[i]);
    }
    printf("\n");

}

void DestinNetworkAlt::setParentBeliefDamping(float gamma){
    if(gamma < 0 || gamma > 1.0){
        throw std::domain_error("setParentBeliefDamping: gamma must be between 0 and 1");
    }
    for(int n = 0; n < destin->nNodes ; n++){
        destin->nodes[n].gamma = gamma;
    }
}

void DestinNetworkAlt::setPreviousBeliefDamping(float lambda){
    if(gamma < 0 || gamma > 1.0){
        throw std::domain_error("setParentBeliefDamping: lambda must be between 0 and 1");
    }
    for(int n = 0; n < destin->nNodes ; n++){
        destin->nodes[n].nLambda = lambda;
    }

}


/** Loads a destin structure from the given file
 * Destroy the current destin structure before loading the new one.
 */
void DestinNetworkAlt::load(const char * fileName){
    if(centroidImages != NULL){
        Cig_DestroyCentroidImages(destin,  centroidImages);
        centroidImages = NULL;
    }

    destin = LoadDestin(destin, fileName);
    if(destin == NULL){
        throw std::runtime_error("load: could not open destin file.\n");
    }


    Node * n = GetNodeFromDestin(destin, 0, 0, 0);
    this->beta = n->beta;
    this->gamma = n->gamma;
    this->isUniform = destin->isUniform;
    this->lambda = n->nLambda;

    if(temperatures != NULL){
        delete [] temperatures;
    }
    temperatures = new float[getLayerCount()];
    memcpy(temperatures, destin->temp, sizeof(float) * getLayerCount());

}

/** Moves the given centroid to its node's input last observation.
  * @param layer
  * @param row
  * @param col
  * @param centroid - centroid to move
  */
void DestinNetworkAlt::moveCentroidToInput(int layer, int row, int col, int centroid){
    Node * n = getNode(layer, row, col);
    if(centroid >= destin->nb[layer]){
        throw std::domain_error("moveCentroidToInput: centroid out of bounds.");
    }
    float * cent = &n->mu[centroid * n->ns];
    memcpy(cent, n->observation, n->ns * sizeof(float));
}

/** Returns the centroid image as a float array.
  * Then displays it.
  */
float * DestinNetworkAlt::getCentroidImage(int layer, int centroid){
    if(!destin->isUniform){
        printf("getCentroidImage: must be uniform");
        return NULL;
    }

    displayCentroidImage(layer, centroid);
    return getCentroidImages()[layer][centroid];
}

/** Gets the generated centroid images as an opencv Mat image.
  */
cv::Mat DestinNetworkAlt::getCentroidImageM(int layer, int centroid, int disp_width, bool enhanceContrast){
    if(!isUniform){
        throw std::logic_error("can't displayCentroidImage with non uniform DeSTIN.");
    }
    if(layer > destin->nLayers ||  centroid > destin->nb[layer]){
        throw std::domain_error("displayCentroidImage: layer, centroid out of bounds\n");
    }

    uint width = Cig_GetCentroidImageWidth(destin, layer);

    //initialize or create new grid if needed
    if(centroidImage.rows != width || centroidImage.cols != width){
        centroidImage = cv::Mat(width, width, CV_32FC1);
    }
    float * data = (float *)centroidImage.data;

    memcpy(data, getCentroidImages()[layer][centroid], width*width*sizeof(float));

    cv::Mat toShow;
    centroidImage.convertTo(toShow, CV_8UC1, 255); // make it suitable for equalizeHist
    if(enhanceContrast){
        cv::equalizeHist(toShow, toShow);
    }

    cv::resize(toShow,centroidImageResized, cv::Size(disp_width, disp_width), 0, 0, cv::INTER_NEAREST);

    return centroidImageResized;
}

/** Generates representative images for all centroids then displays the chosen image.
  * Currently only works for uniform destin. cvWaitKey() must be called after to see the image.
  * The contrast of the image can be enhanced by setting the weight exponent
  * parameter larger than 1.0 via setCentImgWeightExponent() method, and further by
  * passing enhanceContrast parameter as true.
  * @param layer - which layer the centroid belongs
  * @param centroid - which centroid of the given layer to display.
  * @param disp_width - scales the square centroid image to this width to be displayed. Defaults to 256 pixels
  * @param enhanceContrast - if true, the image contrast is enhanced using the opencv function cvEqualizeHist as a post processing step
  * @param window_name - name to give the display window
  */
void DestinNetworkAlt::displayCentroidImage(int layer, int centroid, int disp_width, bool enhanceContrast, string window_name){
    getCentroidImageM(layer, centroid, disp_width, enhanceContrast);
    cv::imshow(window_name.c_str(), centroidImageResized);
}

void DestinNetworkAlt::saveCentroidImage(int layer, int centroid, string filename, int disp_width, bool enhanceContrast){
    getCentroidImageM(layer, centroid, disp_width, enhanceContrast);
    cv::imwrite(filename, centroidImageResized);
}

/** Displays all the centroid images of a layer into one large image.
  * The centroid images are arranged into a grid and are seperated by a black boarder.
  * If there's not a square number of centroid images, then empty (black) centroid images
  * are added to the bottom right of the grid until the number is a square number. For example
  * if there are 15 centroid images, then it will display a 4x4 grid with the bottom right corner empty.
  * Only works with uniform destin.
  * cvWaitKey() must be called after for the image to appear.
  *
  * @param layer - what layer the centroids belong to
  * @param scale_width - resizes the square image grid width to this size.
  * @param boarder_width - how wide, in pixels, the black border is that seperates the images.
  * @param window_title - name given to the window that is displayed.
  */
void DestinNetworkAlt::displayLayerCentroidImages(int layer,
                                int scale_width,
                                int border_width,
                                string window_title
                                ){
    if(!isUniform){
        throw std::logic_error("can't displayLayerCentroidImages with non uniform DeSTIN.");
    }

    int images = getBeliefsPerNode(layer);
    int images_wide = ceil(sqrt(images));
    int sub_img_width = (int)((double)scale_width / (double)images_wide - (double)border_width);

    int wpb = sub_img_width + border_width; // sub image width plus boarder. Each image gets a right and bottom boarder only.

    int images_high = ceil((float)images / (float)images_wide);

    // initialize the big image as solid black
    cv::Mat big_img = cv::Mat::zeros(wpb*images_high, wpb*images_wide, CV_32FC1);

    int r, c, x, y;
    // copies the subimages into the correct place in the big image
    for(int i = 0 ; i < images ; i++){
            r = i  / images_wide;
            c = i - r * images_wide;
            x = c * wpb;
            y = r * wpb;
            int w = Cig_GetCentroidImageWidth(destin, layer);
            cv::Mat subimage(w, w, CV_32FC1, getCentroidImages()[layer][i]);
            cv::Mat subimage_resized;
            cv::resize(subimage, subimage_resized, cv::Size(sub_img_width, sub_img_width), 0,0,cv::INTER_NEAREST);
            cv::Rect roi( cv::Point( x, y ), subimage_resized.size() );
            cv::Mat dest = big_img( roi );

            subimage_resized.copyTo( dest );
    }

    layerCentroidsImage = big_img;
    cv::imshow(window_title, layerCentroidsImage);
}


/** Displays an image representation of the tree.
  * The input tree descibes
  */
void DestinNetworkAlt::displayTree(std::vector<short> & tree){
    //cv::Mat img; //TODO: resize properly
    //displayTreeHelper(tree, 0, 0, 0, img);
    //TODO: finish implementation
    return;
}
